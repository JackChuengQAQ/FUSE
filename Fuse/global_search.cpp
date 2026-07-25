#include "global_search.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>

GlobalSearcher::GlobalSearcher(const EmbeddingDiGraph& kg, bool verbose)
    : kg_(kg),
      verbose_(verbose),
      undirected_mode_(false) {
}

void GlobalSearcher::setRelationNameMap(const std::vector<ui>& rel_q2k) {
    rel_q2k_ = rel_q2k;
}

void GlobalSearcher::setUndirectedMode(bool enabled) {
    undirected_mode_ = enabled;
}

float GlobalSearcher::nodeSimilarity(const float* query_emb, VertexID kg_v) const {
    if (query_emb == NULL || kg_.getVertexEmbeddingDim() == 0) {
        return 0.0f;
    }
    const float* kg_emb = kg_.getVertexEmbedding(kg_v);
    ui dim = kg_.getVertexEmbeddingDim();
    float dist2 = 0.0f;
    for (ui i = 0; i < dim; ++i) {
        float diff = query_emb[i] - kg_emb[i];
        dist2 += diff * diff;
    }
    return -std::sqrt(dist2);
}

bool GlobalSearcher::relationMatch(ui query_rel, ui kg_rel) const {
    if (query_rel >= rel_q2k_.size()) {
        return false;
    }
    ui mapped = rel_q2k_[query_rel];
    if (mapped == std::numeric_limits<ui>::max()) {
        return false;
    }
    return mapped == kg_rel;
}

bool GlobalSearcher::firstMatchedKgEdge(VertexID src, VertexID dst, ui query_rel,
                                       VertexID& out_src, VertexID& out_dst, ui& out_rel) const {
    auto scan = [&](VertexID a, VertexID b) -> bool {
        ui out_count = 0;
        const VertexID* nbs = kg_.getOutNeighbors(a, out_count);
        const ui* eids = kg_.getOutEdgeIds(a, out_count);
        for (ui i = 0; i < out_count; ++i) {
            if (nbs[i] != b) {
                continue;
            }
            ui rel_kg = kg_.getEdgeRelationId(eids[i]);
            if (!relationMatch(query_rel, rel_kg)) {
                continue;
            }
            out_src = a;
            out_dst = b;
            out_rel = rel_kg;
            return true;
        }
        return false;
    };

    if (scan(src, dst)) {
        return true;
    }
    if (undirected_mode_ && scan(dst, src)) {
        return true;
    }
    return false;
}

bool GlobalSearcher::isUsed(const std::vector<VertexID>& mapping, VertexID kg_v) const {
    for (size_t i = 0; i < mapping.size(); ++i) {
        if (mapping[i] == kg_v) {
            return true;
        }
    }
    return false;
}

std::vector<GlobalSearcher::SearchResult> GlobalSearcher::searchTopK(
    const QueryPattern& query,
    const std::vector<const float*>& query_vertex_embeddings,
    const std::vector<VertexID>& bfs_order,
    const std::vector<std::vector<UpperBoundPruner::DagEdge> >& dag_children,
    const std::vector<std::vector<VertexID> >& candidates,
    const std::vector<std::unordered_map<VertexID, float> >& ub_table,
    ui top_k,
    const std::vector<QueryDirectedEdge>* ordered_query_edges) const {
    last_search_stats_ = SearchRunStats();
    std::vector<SearchResult> results;
    if (bfs_order.empty() || top_k == 0) {
        return results;
    }

    struct Cmp {
        bool operator()(const PartialState& a, const PartialState& b) const {
            return a.ub < b.ub;
        }
    };
    std::priority_queue<PartialState, std::vector<PartialState>, Cmp> pq;

    struct SiblingLeafKey {
        VertexID parent;
        ui relation_id;
        bool leaf_to_parent;
    };
    struct SiblingLeafGroup {
        SiblingLeafKey key;
        std::vector<VertexID> leaves;
    };
    auto same_key = [](const SiblingLeafKey& a, const SiblingLeafKey& b) -> bool {
        return a.parent == b.parent && a.relation_id == b.relation_id && a.leaf_to_parent == b.leaf_to_parent;
    };
    auto leaf_key = [&](VertexID u, SiblingLeafKey& key) -> bool {
        if (u >= query.num_vertices) {
            return false;
        }
        const size_t out_deg = query.out_neighbors[u].size();
        const size_t in_deg = query.in_neighbors[u].size();
        if (out_deg + in_deg != 1) {
            return false;
        }
        if (out_deg == 1) {
            key.parent = query.out_neighbors[u][0].neighbor_id;
            key.relation_id = query.out_neighbors[u][0].relation_id;
            key.leaf_to_parent = undirected_mode_ ? false : true;
            return true;
        }
        key.parent = query.in_neighbors[u][0].neighbor_id;
        key.relation_id = query.in_neighbors[u][0].relation_id;
        key.leaf_to_parent = false;
        return true;
    };
    std::vector<SiblingLeafGroup> sibling_groups;
    for (VertexID u = 0; u < query.num_vertices; ++u) {
        SiblingLeafKey key;
        if (!leaf_key(u, key)) {
            continue;
        }
        bool added = false;
        for (size_t gi = 0; gi < sibling_groups.size(); ++gi) {
            if (same_key(sibling_groups[gi].key, key)) {
                sibling_groups[gi].leaves.push_back(u);
                added = true;
                break;
            }
        }
        if (!added) {
            SiblingLeafGroup group;
            group.key = key;
            group.leaves.push_back(u);
            sibling_groups.push_back(group);
        }
    }

    std::vector<int> bfs_pos(query.num_vertices, -1);
    for (size_t i = 0; i < bfs_order.size(); ++i) {
        if (bfs_order[i] < bfs_pos.size()) {
            bfs_pos[bfs_order[i]] = static_cast<int>(i);
        }
    }
    std::vector<int> pack_group_by_node(query.num_vertices, -1);
    const bool enable_sibling_pack = false;
    if (enable_sibling_pack) {
        for (size_t gi = 0; gi < sibling_groups.size(); ++gi) {
            std::sort(sibling_groups[gi].leaves.begin(), sibling_groups[gi].leaves.end(),
                      [&](VertexID a, VertexID b) { return bfs_pos[a] < bfs_pos[b]; });
            for (size_t li = 0; li < sibling_groups[gi].leaves.size(); ++li) {
                VertexID leaf = sibling_groups[gi].leaves[li];
                pack_group_by_node[leaf] = static_cast<int>(gi);
            }
        }
    }

    struct ParentInfo {
        VertexID parent;
        ui relation_id;
        bool query_edge_reversed;
        bool has_parent;
    };
    std::vector<ParentInfo> parent_info(
        query.num_vertices,
        ParentInfo{std::numeric_limits<VertexID>::max(), 0, false, false});
    for (ui p = 0; p < dag_children.size(); ++p) {
        for (size_t ei = 0; ei < dag_children[p].size(); ++ei) {
            const UpperBoundPruner::DagEdge& edge = dag_children[p][ei];
            if (edge.child < parent_info.size() && !parent_info[edge.child].has_parent) {
                parent_info[edge.child] = ParentInfo{p, edge.relation_id, edge.query_edge_reversed, true};
            }
        }
    }

    auto is_descendant_or_self = [&](VertexID x, VertexID ancestor) -> bool {
        VertexID cur = x;
        while (cur < parent_info.size()) {
            if (cur == ancestor) {
                return true;
            }
            if (!parent_info[cur].has_parent) {
                return false;
            }
            cur = parent_info[cur].parent;
        }
        return false;
    };

    auto parent_edge_ok = [&](VertexID child, VertexID child_value,
                              const std::vector<VertexID>& mapping) -> bool {
        if (child >= parent_info.size() || !parent_info[child].has_parent) {
            return true;
        }
        const ParentInfo& pe = parent_info[child];
        if (pe.parent >= mapping.size() ||
            mapping[pe.parent] == std::numeric_limits<VertexID>::max()) {
            return false;
        }
        VertexID dummy_src = 0, dummy_dst = 0;
        ui dummy_rel = 0;
        if (pe.query_edge_reversed) {
            return firstMatchedKgEdge(child_value, mapping[pe.parent], pe.relation_id,
                                      dummy_src, dummy_dst, dummy_rel);
        }
        return firstMatchedKgEdge(mapping[pe.parent], child_value, pe.relation_id,
                                  dummy_src, dummy_dst, dummy_rel);
    };

    std::vector<std::vector<VertexID> > candidates_by_ub(query.num_vertices);
    for (ui u = 0; u < query.num_vertices; ++u) {
        if (u >= candidates.size()) {
            continue;
        }
        candidates_by_ub[u].reserve(candidates[u].size());
        for (size_t ci = 0; ci < candidates[u].size(); ++ci) {
            VertexID v = candidates[u][ci];
            if (u < ub_table.size() && ub_table[u].find(v) != ub_table[u].end()) {
                candidates_by_ub[u].push_back(v);
            }
        }
        std::sort(candidates_by_ub[u].begin(), candidates_by_ub[u].end(),
                  [&](VertexID a, VertexID b) {
                      float ua = ub_table[u].find(a)->second;
                      float ub = ub_table[u].find(b)->second;
                      if (ua != ub) {
                          return ua > ub;
                      }
                      return a < b;
                  });
        candidates_by_ub[u].erase(std::unique(candidates_by_ub[u].begin(), candidates_by_ub[u].end()),
                                  candidates_by_ub[u].end());
    }

    std::vector<std::unordered_map<VertexID, float> > node_score_cache(query.num_vertices);
    for (ui u = 0; u < query.num_vertices; ++u) {
        node_score_cache[u].reserve(candidates_by_ub[u].size() * 2 + 16);
        const float* q_emb = (u < query_vertex_embeddings.size()) ? query_vertex_embeddings[u] : NULL;
        for (size_t i = 0; i < candidates_by_ub[u].size(); ++i) {
            VertexID v = candidates_by_ub[u][i];
            node_score_cache[u][v] = nodeSimilarity(q_emb, v);
        }
    }
    auto cached_node_score = [&](VertexID u, VertexID v) -> float {
        if (u < node_score_cache.size()) {
            std::unordered_map<VertexID, float>::const_iterator it = node_score_cache[u].find(v);
            if (it != node_score_cache[u].end()) {
                return it->second;
            }
        }
        const float* q_emb = (u < query_vertex_embeddings.size()) ? query_vertex_embeddings[u] : NULL;
        return nodeSimilarity(q_emb, v);
    };

    auto best_frontier_ub = [&](VertexID u,
                                const std::vector<VertexID>& mapping) -> float {
        if (u >= candidates_by_ub.size() || u >= ub_table.size()) {
            return -std::numeric_limits<float>::max();
        }
        const std::vector<VertexID>& ordered = candidates_by_ub[u];
        for (size_t ci = 0; ci < ordered.size(); ++ci) {
            VertexID v = ordered[ci];
            if (isUsed(mapping, v)) {
                continue;
            }
            std::unordered_map<VertexID, float>::const_iterator it = ub_table[u].find(v);
            if (it == ub_table[u].end()) {
                continue;
            }
            if (!parent_edge_ok(u, v, mapping)) {
                continue;
            }
            return it->second;
        }
        return -std::numeric_limits<float>::max();
    };

    auto rest_upper_bound = [&](const std::vector<VertexID>& mapping,
                                VertexID current_root) -> float {
        float rest = 0.0f;
        for (ui x = 0; x < query.num_vertices; ++x) {
            if (x >= mapping.size() ||
                mapping[x] != std::numeric_limits<VertexID>::max() ||
                is_descendant_or_self(x, current_root)) {
                continue;
            }
            if (x < parent_info.size() && parent_info[x].has_parent) {
                VertexID p = parent_info[x].parent;
                if (p >= mapping.size() ||
                    mapping[p] == std::numeric_limits<VertexID>::max()) {
                    continue;
                }
            }
            float best = best_frontier_ub(x, mapping);
            if (best <= -std::numeric_limits<float>::max() / 2) {
                return -std::numeric_limits<float>::max();
            }
            rest += best;
        }
        return rest;
    };

    struct MappingCandidate {
        float score;
        std::vector<VertexID> mapping;
    };
    const ui mapping_pool_k = top_k;
    std::vector<MappingCandidate> mapping_candidates;
    mapping_candidates.reserve(mapping_pool_k + 16);

    auto insert_mapping_candidate = [&](float score, const std::vector<VertexID>& mapping) {
        for (size_t i = 0; i < mapping_candidates.size(); ++i) {
            if (mapping_candidates[i].mapping == mapping) {
                if (score > mapping_candidates[i].score) {
                    mapping_candidates[i].score = score;
                }
                std::sort(mapping_candidates.begin(), mapping_candidates.end(),
                          [](const MappingCandidate& a, const MappingCandidate& b) { return a.score > b.score; });
                return;
            }
        }
        mapping_candidates.push_back(MappingCandidate{score, mapping});
        std::sort(mapping_candidates.begin(), mapping_candidates.end(),
                  [](const MappingCandidate& a, const MappingCandidate& b) { return a.score > b.score; });
        if (mapping_candidates.size() > mapping_pool_k) {
            mapping_candidates.resize(mapping_pool_k);
        }
    };
    auto worst_mapping_score = [&]() -> float {
        if (mapping_candidates.empty()) {
            return -std::numeric_limits<float>::max();
        }
        return mapping_candidates.back().score;
    };

    auto structurally_extendable = [&](VertexID u, VertexID v,
                                       const std::vector<VertexID>& mapping) -> bool {
        for (size_t e = 0; e < query.out_neighbors[u].size(); ++e) {
            VertexID x = query.out_neighbors[u][e].neighbor_id;
            ui rel_q = query.out_neighbors[u][e].relation_id;
            if (mapping[x] == std::numeric_limits<VertexID>::max()) {
                continue;
            }
            VertexID dummy_src = 0, dummy_dst = 0;
            ui dummy_rel = 0;
            if (!firstMatchedKgEdge(v, mapping[x], rel_q, dummy_src, dummy_dst, dummy_rel)) {
                return false;
            }
        }
        for (size_t e = 0; e < query.in_neighbors[u].size(); ++e) {
            VertexID x = query.in_neighbors[u][e].neighbor_id;
            ui rel_q = query.in_neighbors[u][e].relation_id;
            if (mapping[x] == std::numeric_limits<VertexID>::max()) {
                continue;
            }
            VertexID dummy_src = 0, dummy_dst = 0;
            ui dummy_rel = 0;
            if (!firstMatchedKgEdge(mapping[x], v, rel_q, dummy_src, dummy_dst, dummy_rel)) {
                return false;
            }
        }
        return true;
    };

    bool use_dfs_main_search = true;
    const char* search_mode_env = std::getenv("FUSE_SEARCH_MODE");
    if (search_mode_env != NULL) {
        std::string mode(search_mode_env);
        if (mode == "pq" || mode == "PQ" || mode == "bestfirst" || mode == "heap") {
            use_dfs_main_search = false;
        } else if (mode == "dfs" || mode == "DFS" || mode == "backtrack") {
            use_dfs_main_search = true;
        }
    }

    if (use_dfs_main_search) {
        bool dfs_enable_ub_prune = true;
        const char* dfs_prune_env = std::getenv("FUSE_DFS_PRUNE");
        if (dfs_prune_env != NULL) {
            std::string prune_mode(dfs_prune_env);
            if (prune_mode == "0" || prune_mode == "false" || prune_mode == "FALSE" ||
                prune_mode == "off" || prune_mode == "OFF") {
                dfs_enable_ub_prune = false;
            }
        }

        struct DfsCandidate {
            VertexID v;
            float branch_ub;
            float node_score;
        };
        struct MappedConstraint {
            VertexID mapped_v;
            ui relation_id;
            bool candidate_to_mapped;
        };
        struct DfsDebugStats {
            uint64_t generate_calls = 0;
            uint64_t full_source_calls = 0;
            uint64_t seeded_source_calls = 0;
            uint64_t source_scanned = 0;
            uint64_t emitted_candidates = 0;
            uint64_t candidate_set_miss = 0;
            uint64_t used_skip = 0;
            uint64_t no_ub_skip = 0;
            uint64_t struct_fail = 0;
            uint64_t prune_breaks = 0;
            uint64_t prune_tail_skipped = 0;
            uint64_t max_source_size = 0;
            uint64_t max_emitted = 0;
            double seed_ms = 0.0;
            double rest_ub_ms = 0.0;
            double filter_ms = 0.0;
            double sort_ms = 0.0;
        };
        DfsDebugStats dfs_debug;
        auto elapsed_debug_ms = [](const std::chrono::high_resolution_clock::time_point& t0) -> double {
            const std::chrono::high_resolution_clock::time_point t1 =
                std::chrono::high_resolution_clock::now();
            return static_cast<double>(
                       std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) /
                   1000.0;
        };

        const VertexID kg_vertices = kg_.getVerticesCount();
        std::vector<unsigned int> seed_seen(kg_vertices, 0);
        unsigned int seed_stamp = 1;
        auto next_seed_stamp = [&]() {
            ++seed_stamp;
            if (seed_stamp == 0) {
                std::fill(seed_seen.begin(), seed_seen.end(), 0);
                seed_stamp = 1;
            }
        };

        auto collect_constraint_candidates = [&](const MappedConstraint& c,
                                                 std::vector<VertexID>& out) {
            next_seed_stamp();
            auto add_one_direction = [&](const VertexID* nbs, const ui* eids, ui count) {
                for (ui i = 0; i < count; ++i) {
                    if (!relationMatch(c.relation_id, kg_.getEdgeRelationId(eids[i]))) {
                        continue;
                    }
                    VertexID v = nbs[i];
                    if (v >= kg_vertices || seed_seen[v] == seed_stamp) {
                        continue;
                    }
                    seed_seen[v] = seed_stamp;
                    out.push_back(v);
                }
            };

            if (c.candidate_to_mapped) {
                ui in_count = 0;
                const VertexID* in_nbs = kg_.getInNeighbors(c.mapped_v, in_count);
                const ui* in_eids = kg_.getInEdgeIds(c.mapped_v, in_count);
                add_one_direction(in_nbs, in_eids, in_count);
                if (undirected_mode_) {
                    ui out_count = 0;
                    const VertexID* out_nbs = kg_.getOutNeighbors(c.mapped_v, out_count);
                    const ui* out_eids = kg_.getOutEdgeIds(c.mapped_v, out_count);
                    add_one_direction(out_nbs, out_eids, out_count);
                }
            } else {
                ui out_count = 0;
                const VertexID* out_nbs = kg_.getOutNeighbors(c.mapped_v, out_count);
                const ui* out_eids = kg_.getOutEdgeIds(c.mapped_v, out_count);
                add_one_direction(out_nbs, out_eids, out_count);
                if (undirected_mode_) {
                    ui in_count = 0;
                    const VertexID* in_nbs = kg_.getInNeighbors(c.mapped_v, in_count);
                    const ui* in_eids = kg_.getInEdgeIds(c.mapped_v, in_count);
                    add_one_direction(in_nbs, in_eids, in_count);
                }
            }
        };

        auto constraint_seed_size = [&](const MappedConstraint& c) -> ui {
            ui total = 0;
            ui count = 0;
            if (c.candidate_to_mapped) {
                kg_.getInNeighbors(c.mapped_v, count);
                total += count;
                if (undirected_mode_) {
                    kg_.getOutNeighbors(c.mapped_v, count);
                    total += count;
                }
            } else {
                kg_.getOutNeighbors(c.mapped_v, count);
                total += count;
                if (undirected_mode_) {
                    kg_.getInNeighbors(c.mapped_v, count);
                    total += count;
                }
            }
            return total;
        };

        auto generate_dfs_candidates = [&](VertexID u,
                                           const std::vector<VertexID>& mapping,
                                           float score,
                                           std::vector<DfsCandidate>& out) {
            out.clear();
            if (verbose_) {
                ++dfs_debug.generate_calls;
            }
            if (u >= candidates_by_ub.size() || u >= ub_table.size()) {
                return;
            }

            std::vector<MappedConstraint> constraints;
            constraints.reserve(query.out_neighbors[u].size() + query.in_neighbors[u].size());
            for (size_t e = 0; e < query.out_neighbors[u].size(); ++e) {
                VertexID x = query.out_neighbors[u][e].neighbor_id;
                if (x < mapping.size() && mapping[x] != std::numeric_limits<VertexID>::max()) {
                    constraints.push_back(MappedConstraint{mapping[x], query.out_neighbors[u][e].relation_id, true});
                }
            }
            for (size_t e = 0; e < query.in_neighbors[u].size(); ++e) {
                VertexID x = query.in_neighbors[u][e].neighbor_id;
                if (x < mapping.size() && mapping[x] != std::numeric_limits<VertexID>::max()) {
                    constraints.push_back(MappedConstraint{mapping[x], query.in_neighbors[u][e].relation_id, false});
                }
            }

            std::vector<VertexID> seed;
            bool use_seed_filter = false;
            unsigned int active_seed_stamp = 0;
            size_t seeded_constraint = std::numeric_limits<size_t>::max();
            size_t source_size_hint = candidates_by_ub[u].size();
            if (!constraints.empty()) {
                if (verbose_) {
                    ++dfs_debug.seeded_source_calls;
                }
                size_t best = 0;
                ui best_size = constraint_seed_size(constraints[0]);
                for (size_t i = 1; i < constraints.size(); ++i) {
                    ui cur_size = constraint_seed_size(constraints[i]);
                    if (cur_size < best_size) {
                        best_size = cur_size;
                        best = i;
                    }
                }
                seed.reserve(best_size + 16);
                std::chrono::high_resolution_clock::time_point seed_t0;
                if (verbose_) {
                    seed_t0 = std::chrono::high_resolution_clock::now();
                }
                collect_constraint_candidates(constraints[best], seed);
                if (verbose_) {
                    dfs_debug.seed_ms += elapsed_debug_ms(seed_t0);
                }
                use_seed_filter = true;
                active_seed_stamp = seed_stamp;
                seeded_constraint = best;
                source_size_hint = seed.size();
            } else if (verbose_) {
                ++dfs_debug.full_source_calls;
            }

            std::chrono::high_resolution_clock::time_point rest_t0;
            if (verbose_) {
                rest_t0 = std::chrono::high_resolution_clock::now();
            }
            float state_rest = rest_upper_bound(mapping, u);
            if (verbose_) {
                dfs_debug.rest_ub_ms += elapsed_debug_ms(rest_t0);
            }
            if (state_rest <= -std::numeric_limits<float>::max() / 2) {
                return;
            }

            const std::vector<VertexID>& ordered = candidates_by_ub[u];
            out.reserve(std::min(source_size_hint, ordered.size()));
            if (verbose_) {
                if (source_size_hint > dfs_debug.max_source_size) {
                    dfs_debug.max_source_size = source_size_hint;
                }
            }
            auto mapped_constraints_ok = [&](VertexID v) -> bool {
                for (size_t ci = 0; ci < constraints.size(); ++ci) {
                    if (use_seed_filter && ci == seeded_constraint) {
                        continue;
                    }
                    const MappedConstraint& c = constraints[ci];
                    VertexID dummy_src = 0, dummy_dst = 0;
                    ui dummy_rel = 0;
                    bool ok = c.candidate_to_mapped
                        ? firstMatchedKgEdge(v, c.mapped_v, c.relation_id,
                                             dummy_src, dummy_dst, dummy_rel)
                        : firstMatchedKgEdge(c.mapped_v, v, c.relation_id,
                                             dummy_src, dummy_dst, dummy_rel);
                    if (!ok) {
                        return false;
                    }
                }
                return true;
            };
            std::chrono::high_resolution_clock::time_point filter_t0;
            if (verbose_) {
                filter_t0 = std::chrono::high_resolution_clock::now();
            }
            for (size_t i = 0; i < ordered.size(); ++i) {
                if (verbose_) {
                    ++dfs_debug.source_scanned;
                }
                VertexID v = ordered[i];
                std::unordered_map<VertexID, float>::const_iterator ub_it = ub_table[u].find(v);
                if (ub_it == ub_table[u].end()) {
                    if (verbose_) {
                        ++dfs_debug.no_ub_skip;
                    }
                    continue;
                }
                float branch_ub = score + ub_it->second + state_rest;
                if (dfs_enable_ub_prune &&
                    mapping_candidates.size() >= mapping_pool_k &&
                    branch_ub <= worst_mapping_score()) {
                    if (verbose_) {
                        ++dfs_debug.prune_breaks;
                        dfs_debug.prune_tail_skipped += ordered.size() - i;
                    }
                    break;
                }
                if (use_seed_filter &&
                    (v >= kg_vertices || seed_seen[v] != active_seed_stamp)) {
                    continue;
                }
                if (isUsed(mapping, v)) {
                    if (verbose_) {
                        ++dfs_debug.used_skip;
                    }
                    continue;
                }
                if (!mapped_constraints_ok(v)) {
                    if (verbose_) {
                        ++dfs_debug.struct_fail;
                    }
                    continue;
                }
                DfsCandidate cand;
                cand.v = v;
                cand.branch_ub = branch_ub;
                cand.node_score = cached_node_score(u, v);
                out.push_back(cand);
            }
            if (verbose_) {
                dfs_debug.filter_ms += elapsed_debug_ms(filter_t0);
                dfs_debug.emitted_candidates += out.size();
                if (out.size() > dfs_debug.max_emitted) {
                    dfs_debug.max_emitted = out.size();
                }
            }
            std::chrono::high_resolution_clock::time_point sort_t0;
            if (verbose_) {
                sort_t0 = std::chrono::high_resolution_clock::now();
            }
            std::sort(out.begin(), out.end(), [](const DfsCandidate& a, const DfsCandidate& b) {
                if (a.branch_ub != b.branch_ub) {
                    return a.branch_ub > b.branch_ub;
                }
                if (a.node_score != b.node_score) {
                    return a.node_score > b.node_score;
                }
                return a.v < b.v;
            });
            if (verbose_) {
                dfs_debug.sort_ms += elapsed_debug_ms(sort_t0);
            }
        };

        std::vector<VertexID> mapping(query.num_vertices, std::numeric_limits<VertexID>::max());
        std::vector<DfsCandidate> level_candidates;
        std::function<void(size_t, float)> dfs_search = [&](size_t depth, float score) {
            ++last_search_stats_.states_popped;
            if (depth >= bfs_order.size()) {
                insert_mapping_candidate(score, mapping);
                return;
            }

            VertexID u = bfs_order[depth];
            if (u >= query.num_vertices) {
                return;
            }
            if (mapping[u] != std::numeric_limits<VertexID>::max()) {
                dfs_search(depth + 1, score);
                return;
            }

            std::vector<DfsCandidate> local_candidates;
            generate_dfs_candidates(u, mapping, score, local_candidates);

            for (size_t i = 0; i < local_candidates.size(); ++i) {
                const DfsCandidate& cand = local_candidates[i];
                if (dfs_enable_ub_prune &&
                    mapping_candidates.size() >= mapping_pool_k &&
                    cand.branch_ub <= worst_mapping_score()) {
                    if (verbose_) {
                        ++dfs_debug.prune_breaks;
                        dfs_debug.prune_tail_skipped += local_candidates.size() - i;
                    }
                    break;
                }
                mapping[u] = cand.v;
                ++last_search_stats_.states_pushed;
                dfs_search(depth + 1, score + cand.node_score);
                mapping[u] = std::numeric_limits<VertexID>::max();
            }
        };

        if (verbose_) {
            std::cerr << "[search-mode] generated-dfs"
                      << " ub_prune=" << (dfs_enable_ub_prune ? 1 : 0)
                      << std::endl;
        }
        dfs_search(0, 0.0f);
        if (verbose_) {
            std::cerr << "[dfs-debug] generate_calls=" << dfs_debug.generate_calls
                      << " full_source_calls=" << dfs_debug.full_source_calls
                      << " seeded_source_calls=" << dfs_debug.seeded_source_calls
                      << " source_scanned=" << dfs_debug.source_scanned
                      << " emitted=" << dfs_debug.emitted_candidates
                      << " candidate_set_miss=" << dfs_debug.candidate_set_miss
                      << " used_skip=" << dfs_debug.used_skip
                      << " no_ub_skip=" << dfs_debug.no_ub_skip
                      << " struct_fail=" << dfs_debug.struct_fail
                      << " prune_breaks=" << dfs_debug.prune_breaks
                      << " prune_tail_skipped=" << dfs_debug.prune_tail_skipped
                      << " max_source_size=" << dfs_debug.max_source_size
                      << " max_emitted=" << dfs_debug.max_emitted
                      << " seed_ms=" << dfs_debug.seed_ms
                      << " rest_ub_ms=" << dfs_debug.rest_ub_ms
                      << " filter_ms=" << dfs_debug.filter_ms
                      << " sort_ms=" << dfs_debug.sort_ms
                      << std::endl;
        }
    } else {
    size_t warm_checks = 0;
    const size_t warm_check_budget = 200000;
    std::vector<VertexID> warm_mapping(query.num_vertices, std::numeric_limits<VertexID>::max());
    std::function<void(size_t, float)> warm_start_dfs = [&](size_t idx, float score) {
        if (mapping_candidates.size() >= mapping_pool_k || warm_checks >= warm_check_budget) {
            return;
        }
        if (idx >= bfs_order.size()) {
            insert_mapping_candidate(score, warm_mapping);
            return;
        }
        VertexID u = bfs_order[idx];
        if (u >= candidates_by_ub.size()) {
            return;
        }
        const std::vector<VertexID>& ordered = candidates_by_ub[u];
        for (size_t ci = 0; ci < ordered.size(); ++ci) {
            if (mapping_candidates.size() >= mapping_pool_k || warm_checks >= warm_check_budget) {
                break;
            }
            ++warm_checks;
            VertexID v = ordered[ci];
            if (isUsed(warm_mapping, v)) {
                continue;
            }
            if (!structurally_extendable(u, v, warm_mapping)) {
                continue;
            }
            warm_mapping[u] = v;
            warm_start_dfs(idx + 1, score + cached_node_score(u, v));
            warm_mapping[u] = std::numeric_limits<VertexID>::max();
        }
    };
    warm_start_dfs(0, 0.0f);
    if (verbose_) {
        std::cerr << "[warm-start] found=" << mapping_candidates.size()
                  << "/" << mapping_pool_k
                  << " checks=" << warm_checks
                  << " tau=" << (mapping_candidates.size() >= mapping_pool_k ? worst_mapping_score() : 0.0f)
                  << std::endl;
    }

    VertexID root = bfs_order[0];
    size_t ub_debug_reports = 0;
    const size_t ub_debug_report_limit = 40;
    if (root >= candidates_by_ub.size()) {
        return results;
    }
    std::vector<VertexID> empty_mapping(query.num_vertices, std::numeric_limits<VertexID>::max());
    float root_rest = rest_upper_bound(empty_mapping, root);
    for (size_t i = 0; i < candidates_by_ub[root].size(); ++i) {
        VertexID v = candidates_by_ub[root][i];
        std::unordered_map<VertexID, float>::const_iterator root_ub_it = ub_table[root].find(v);
        if (root_ub_it == ub_table[root].end() ||
            root_rest <= -std::numeric_limits<float>::max() / 2) {
            continue;
        }
        std::vector<VertexID> mapping(query.num_vertices, std::numeric_limits<VertexID>::max());
        float ub = root_ub_it->second + root_rest;
        if (ub <= -std::numeric_limits<float>::max() / 2) {
            continue;
        }
        if (mapping_candidates.size() >= mapping_pool_k && ub <= worst_mapping_score()) {
            break;
        }
        mapping[root] = v;
        float cur = cached_node_score(root, v);
        pq.push(PartialState{ub, cur, mapping, 1});
        ++last_search_stats_.states_pushed;
    }

    while (!pq.empty()) {
        PartialState s = pq.top();
        pq.pop();
        ++last_search_stats_.states_popped;

        if (mapping_candidates.size() >= mapping_pool_k && s.ub <= worst_mapping_score()) {
            break;
        }

        if (s.next_idx >= bfs_order.size()) {
            insert_mapping_candidate(s.current_score, s.mapping);
            continue;
        }

        VertexID u = bfs_order[s.next_idx];
        if (u < pack_group_by_node.size() && pack_group_by_node[u] >= 0) {
            const SiblingLeafGroup& group = sibling_groups[static_cast<size_t>(pack_group_by_node[u])];
            if (!group.leaves.empty() && u == group.leaves.front()) {
                VertexID parent = group.key.parent;
                if (parent < s.mapping.size() && s.mapping[parent] != std::numeric_limits<VertexID>::max()) {
                    VertexID v_parent = s.mapping[parent];
                    struct PackedOption {
                        float score;
                        std::vector<VertexID> values;
                    };
                    std::vector<std::vector<std::pair<VertexID, float> > > options(group.leaves.size());
                    bool feasible_group = true;
                    for (size_t li = 0; li < group.leaves.size(); ++li) {
                        VertexID leaf = group.leaves[li];
                        for (size_t ci = 0; ci < candidates[leaf].size(); ++ci) {
                            VertexID v = candidates[leaf][ci];
                            if (isUsed(s.mapping, v)) {
                                continue;
                            }
                            bool dup_in_group = false;
                            
                            
                            (void)dup_in_group;
                            VertexID dummy_src = 0, dummy_dst = 0;
                            ui dummy_rel = 0;
                            bool edge_ok = group.key.leaf_to_parent
                                ? firstMatchedKgEdge(v, v_parent, group.key.relation_id, dummy_src, dummy_dst, dummy_rel)
                                : firstMatchedKgEdge(v_parent, v, group.key.relation_id, dummy_src, dummy_dst, dummy_rel);
                            if (!edge_ok) {
                                continue;
                            }
                            options[li].push_back(std::make_pair(v, cached_node_score(leaf, v)));
                        }
                        std::sort(options[li].begin(), options[li].end(),
                                  [](const std::pair<VertexID, float>& a, const std::pair<VertexID, float>& b) {
                                      if (a.second != b.second) {
                                          return a.second > b.second;
                                      }
                                      return a.first < b.first;
                                  });
                        if (options[li].empty()) {
                            feasible_group = false;
                            break;
                        }
                    }

                    if (!feasible_group) {
                        continue;
                    }

                    std::vector<PackedOption> beam(1);
                    beam[0].score = 0.0f;
                    beam[0].values.assign(group.leaves.size(), std::numeric_limits<VertexID>::max());
                    for (size_t li = 0; li < group.leaves.size(); ++li) {
                        std::vector<PackedOption> next_beam;
                        next_beam.reserve(top_k * options[li].size());
                        for (size_t bi = 0; bi < beam.size(); ++bi) {
                            for (size_t oi = 0; oi < options[li].size(); ++oi) {
                                VertexID v = options[li][oi].first;
                                bool used_in_pack = false;
                                for (size_t prev = 0; prev < li; ++prev) {
                                    if (beam[bi].values[prev] == v) {
                                        used_in_pack = true;
                                        break;
                                    }
                                }
                                if (used_in_pack) {
                                    continue;
                                }
                                PackedOption candidate = beam[bi];
                                candidate.values[li] = v;
                                candidate.score += options[li][oi].second;
                                next_beam.push_back(candidate);
                            }
                        }
                        std::sort(next_beam.begin(), next_beam.end(),
                                  [](const PackedOption& a, const PackedOption& b) {
                                      if (a.score != b.score) {
                                          return a.score > b.score;
                                      }
                                      return a.values < b.values;
                                  });
                        if (next_beam.size() > top_k) {
                            next_beam.resize(top_k);
                        }
                        beam.swap(next_beam);
                        if (beam.empty()) {
                            break;
                        }
                    }
                    const std::vector<PackedOption>& packed = beam;

                    for (size_t pi = 0; pi < packed.size(); ++pi) {
                        std::vector<VertexID> mapping2 = s.mapping;
                        for (size_t li = 0; li < group.leaves.size(); ++li) {
                            mapping2[group.leaves[li]] = packed[pi].values[li];
                        }
                        float cur2 = s.current_score + packed[pi].score;
                        ui next_next_idx = s.next_idx + 1;
                        if (next_next_idx > bfs_order.size()) {
                            continue;
                        }
                        float ub2 = s.ub;
                        if (mapping_candidates.size() >= mapping_pool_k && ub2 <= worst_mapping_score()) {
                            continue;
                        }
                        pq.push(PartialState{ub2, cur2, mapping2, next_next_idx});
                        ++last_search_stats_.states_pushed;
                    }
                    continue;
                }
            } else if (s.mapping[u] != std::numeric_limits<VertexID>::max()) {
                PartialState next = s;
                next.next_idx += 1;
                pq.push(next);
                ++last_search_stats_.states_pushed;
                continue;
            }
        }

        const bool collect_ub_debug = verbose_ && ub_debug_reports < ub_debug_report_limit;
        const bool debug_has_tau = mapping_candidates.size() >= mapping_pool_k;
        const float debug_tau = debug_has_tau ? worst_mapping_score() : 0.0f;
        size_t debug_candidates = collect_ub_debug && u < candidates_by_ub.size() ? candidates_by_ub[u].size() : 0;
        size_t debug_used_skip = 0;
        size_t debug_struct_ok = 0;
        size_t debug_no_ub = 0;
        size_t debug_valid_ub_no_tau = 0;
        size_t debug_ub_gt_tau = 0;
        size_t debug_ub_le_tau = 0;
        size_t debug_ub_break_skip = 0;
        float debug_max_ub = -std::numeric_limits<float>::max();
        float state_rest = rest_upper_bound(s.mapping, u);
        if (state_rest <= -std::numeric_limits<float>::max() / 2) {
            continue;
        }

        if (u >= candidates_by_ub.size()) {
            continue;
        }
        const std::vector<VertexID>& ordered_candidates = candidates_by_ub[u];
        for (size_t i = 0; i < ordered_candidates.size(); ++i) {
            VertexID v = ordered_candidates[i];
            std::unordered_map<VertexID, float>::const_iterator ub_it = ub_table[u].find(v);
            if (ub_it == ub_table[u].end()) {
                if (collect_ub_debug) {
                    ++debug_no_ub;
                }
                continue;
            }

            float ub2 = s.current_score + ub_it->second + state_rest;
            if (collect_ub_debug) {
                if (ub2 > debug_max_ub) {
                    debug_max_ub = ub2;
                }
                if (!debug_has_tau) {
                    ++debug_valid_ub_no_tau;
                } else if (ub2 > debug_tau) {
                    ++debug_ub_gt_tau;
                } else {
                    ++debug_ub_le_tau;
                }
            }
            if (mapping_candidates.size() >= mapping_pool_k && ub2 <= worst_mapping_score()) {
                if (collect_ub_debug) {
                    debug_ub_break_skip = ordered_candidates.size() - i;
                }
                break;
            }

            if (isUsed(s.mapping, v)) {
                if (collect_ub_debug) {
                    ++debug_used_skip;
                }
                continue;
            }

            if (!structurally_extendable(u, v, s.mapping)) {
                continue;
            }
            if (collect_ub_debug) {
                ++debug_struct_ok;
            }

            if (collect_ub_debug) {
                if (((i + 1) % 500) == 0) {
                    std::cerr << "[ub-prune-progress] popped=" << last_search_stats_.states_popped
                              << " next_idx=" << s.next_idx
                              << " u=" << u
                              << " processed=" << (i + 1)
                              << "/" << debug_candidates
                              << " topk_ready=" << (debug_has_tau ? 1 : 0)
                              << " tau=" << debug_tau
                              << " struct_ok=" << debug_struct_ok
                              << " valid_ub_no_tau=" << debug_valid_ub_no_tau
                              << " ub_gt_tau=" << debug_ub_gt_tau
                              << " ub_le_tau=" << debug_ub_le_tau
                              << " max_ub=" << debug_max_ub
                              << std::endl;
                }
            }

            std::vector<VertexID> mapping2 = s.mapping;
            mapping2[u] = v;
            float cur2 = s.current_score + cached_node_score(u, v);
            ui next_next_idx = s.next_idx + 1;
            if (next_next_idx > bfs_order.size()) {
                continue;
            }
            pq.push(PartialState{ub2, cur2, mapping2, s.next_idx + 1});
            ++last_search_stats_.states_pushed;
        }

        if (collect_ub_debug) {
            std::cerr << "[ub-prune-debug] popped=" << last_search_stats_.states_popped
                      << " next_idx=" << s.next_idx
                      << " u=" << u
                      << " tau=" << debug_tau
                      << " state_ub=" << s.ub
                      << " cur=" << s.current_score
                      << " candidates=" << debug_candidates
                      << " used_skip=" << debug_used_skip
                      << " struct_ok=" << debug_struct_ok
                      << " no_ub=" << debug_no_ub
                      << " valid_ub_no_tau=" << debug_valid_ub_no_tau
                      << " ub_gt_tau=" << debug_ub_gt_tau
                      << " ub_le_tau=" << debug_ub_le_tau
                      << " ub_break_skip=" << debug_ub_break_skip
                      << " max_ub=" << debug_max_ub
                      << " pq=" << pq.size()
                      << std::endl;
            ++ub_debug_reports;
        }
    }

    }

    
    
    std::vector<QueryDirectedEdge> query_edges;
    if (ordered_query_edges != NULL && !ordered_query_edges->empty()) {
        query_edges = *ordered_query_edges;
    } else {
        for (ui u = 0; u < query.num_vertices; ++u) {
            for (size_t e = 0; e < query.out_neighbors[u].size(); ++e) {
                query_edges.push_back(QueryDirectedEdge{
                    u,
                    query.out_neighbors[u][e].neighbor_id,
                    query.out_neighbors[u][e].relation_id
                });
            }
        }
    }

    for (size_t mi = 0; mi < mapping_candidates.size(); ++mi) {
        const std::vector<VertexID>& mapping = mapping_candidates[mi].mapping;
        SearchResult r;
        r.score = mapping_candidates[mi].score;
        r.mapping = mapping;
        r.node_names.resize(mapping.size());
        for (size_t u = 0; u < mapping.size(); ++u) {
            if (mapping[u] == std::numeric_limits<VertexID>::max()) {
                r.node_names[u] = "<unmatched>";
            } else {
                r.node_names[u] = kg_.getVertexName(mapping[u]);
            }
        }

        bool feasible = true;
        for (size_t ei = 0; ei < query_edges.size(); ++ei) {
            const QueryDirectedEdge& qe = query_edges[ei];
            if (qe.src >= mapping.size() || qe.dst >= mapping.size()) {
                feasible = false;
                break;
            }
            VertexID vs = mapping[qe.src];
            VertexID vd = mapping[qe.dst];
            if (vs == std::numeric_limits<VertexID>::max() || vd == std::numeric_limits<VertexID>::max()) {
                feasible = false;
                break;
            }
            VertexID ksrc = vs, kdst = vd;
            ui krel = std::numeric_limits<ui>::max();
            if (!firstMatchedKgEdge(vs, vd, qe.relation_id, ksrc, kdst, krel)) {
                feasible = false;
                break;
            }
            r.matched_edges.push_back(std::make_tuple(
                kg_.getVertexName(ksrc),
                kg_.getRelationName(krel),
                kg_.getVertexName(kdst),
                0.0f));
        }
        if (!feasible) {
            continue;
        }
        results.push_back(r);
    }

    if (verbose_) {
        std::cerr << "[global] mappings=" << mapping_candidates.size()
                  << " results=" << results.size() << std::endl;
    }
    
    for (size_t i = 0; i < results.size(); ++i) {
        results[i].score = -results[i].score;
        for (size_t e = 0; e < results[i].matched_edges.size(); ++e) {
            std::get<3>(results[i].matched_edges[e]) = -std::get<3>(results[i].matched_edges[e]);
        }
    }
    return results;
}
