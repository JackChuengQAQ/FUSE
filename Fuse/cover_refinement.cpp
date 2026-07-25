#include "cover_refinement.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
inline uint64_t packDirectedEdge(VertexID src, VertexID dst, ui rel) {
    return (uint64_t(src) << 42) | (uint64_t(dst) << 21) | uint64_t(rel);
}
}

VertexCoverRefiner::VertexCoverRefiner(const EmbeddingDiGraph& kg, bool verbose)
    : kg_(kg),
      undirected_mode_(false),
      log_mode_(verbose ? LogMode::Debug : LogMode::Silent) {
}

QueryPattern VertexCoverRefiner::buildQueryPattern(ui num_vertices, const std::vector<QueryDirectedEdge>& edges) {
    QueryPattern pattern;
    pattern.num_vertices = num_vertices;
    pattern.out_neighbors.assign(num_vertices, std::vector<QueryNeighborInfo>());
    pattern.in_neighbors.assign(num_vertices, std::vector<QueryNeighborInfo>());

    for (size_t i = 0; i < edges.size(); ++i) {
        const QueryDirectedEdge& e = edges[i];
        if (e.src >= num_vertices || e.dst >= num_vertices || e.src == e.dst) {
            continue;
        }
        pattern.out_neighbors[e.src].push_back(QueryNeighborInfo{e.dst, e.relation_id});
        pattern.in_neighbors[e.dst].push_back(QueryNeighborInfo{e.src, e.relation_id});
    }
    return pattern;
}

std::unordered_set<VertexID> VertexCoverRefiner::buildIndependentSet(
    ui num_vertices, const std::unordered_set<VertexID>& cover_set) {
    std::unordered_set<VertexID> independent_set;
    independent_set.reserve(num_vertices);
    for (ui u = 0; u < num_vertices; ++u) {
        if (!cover_set.count(u)) {
            independent_set.insert(u);
        }
    }
    return independent_set;
}

void VertexCoverRefiner::setRelationNameMap(const std::vector<ui>& rel_q2k) {
    rel_q2k_ = rel_q2k;
}

void VertexCoverRefiner::setQueryNodeLabels(const std::vector<std::unordered_set<LabelID> >& node_kg_labels) {
    node_kg_labels_ = node_kg_labels;
}

void VertexCoverRefiner::setUndirectedMode(bool enabled) {
    undirected_mode_ = enabled;
}

void VertexCoverRefiner::setLogMode(LogMode mode) {
    log_mode_ = mode;
}

void VertexCoverRefiner::setDebugMode(bool enabled) {
    log_mode_ = enabled ? LogMode::Debug : LogMode::Silent;
}

void VertexCoverRefiner::setLabelInvertedIndex(const std::vector<std::vector<VertexID> >* idx) {
    label_to_vertices_ = idx;
}

void VertexCoverRefiner::setVamanaIndex(const VamanaANN* ann) {
    ann_ = ann;
}

bool VertexCoverRefiner::relationMatch(ui query_rel, ui kg_rel) const {
    if (query_rel >= rel_q2k_.size()) {
        return false;
    }
    ui mapped = rel_q2k_[query_rel];
    if (mapped == std::numeric_limits<ui>::max()) {
        return false;
    }
    return mapped == kg_rel;
}

bool VertexCoverRefiner::labelOk(VertexID query_node, VertexID kg_v) const {
    (void)query_node;
    (void)kg_v;
    return true;
}

std::vector<VertexCoverRefiner::RelationDegreeRequirement>
VertexCoverRefiner::buildRelationDegreeRequirements(VertexID query_node, const QueryPattern& query) const {
    std::vector<RelationDegreeRequirement> requirements;
    if (query_node >= query.num_vertices) {
        return requirements;
    }

    auto add_required = [&](ui relation_id, bool outgoing) {
        ui mapped = std::numeric_limits<ui>::max();
        if (relation_id < rel_q2k_.size()) {
            mapped = rel_q2k_[relation_id];
        }
        if (mapped == std::numeric_limits<ui>::max()) {
            requirements.push_back(RelationDegreeRequirement{mapped, 1, 0, 0});
            return;
        }

        for (size_t i = 0; i < requirements.size(); ++i) {
            if (requirements[i].relation_id == mapped) {
                if (undirected_mode_) {
                    requirements[i].any_required += 1;
                } else if (outgoing) {
                    requirements[i].out_required += 1;
                } else {
                    requirements[i].in_required += 1;
                }
                return;
            }
        }

        RelationDegreeRequirement req{mapped, 0, 0, 0};
        if (undirected_mode_) {
            req.any_required = 1;
        } else if (outgoing) {
            req.out_required = 1;
        } else {
            req.in_required = 1;
        }
        requirements.push_back(req);
    };

    for (size_t i = 0; i < query.out_neighbors[query_node].size(); ++i) {
        add_required(query.out_neighbors[query_node][i].relation_id, true);
    }
    for (size_t i = 0; i < query.in_neighbors[query_node].size(); ++i) {
        add_required(query.in_neighbors[query_node][i].relation_id, false);
    }
    return requirements;
}

bool VertexCoverRefiner::relationDegreeOk(
    VertexID kg_v,
    const std::vector<RelationDegreeRequirement>& requirements) const {
    if (requirements.empty()) {
        return true;
    }

    for (size_t r = 0; r < requirements.size(); ++r) {
        const RelationDegreeRequirement& req = requirements[r];
        if (req.relation_id == std::numeric_limits<ui>::max()) {
            return false;
        }

        ui out_match = 0;
        ui in_match = 0;
        ui edge_count = 0;

        const ui* out_eids = kg_.getOutEdgeIds(kg_v, edge_count);
        for (ui i = 0; i < edge_count; ++i) {
            if (kg_.getEdgeRelationId(out_eids[i]) == req.relation_id) {
                out_match += 1;
            }
        }

        const ui* in_eids = kg_.getInEdgeIds(kg_v, edge_count);
        for (ui i = 0; i < edge_count; ++i) {
            if (kg_.getEdgeRelationId(in_eids[i]) == req.relation_id) {
                in_match += 1;
            }
        }

        if (undirected_mode_) {
            if (out_match + in_match < req.any_required) {
                return false;
            }
        } else if (out_match < req.out_required || in_match < req.in_required) {
            return false;
        }
    }
    return true;
}

std::unordered_set<VertexID> VertexCoverRefiner::computeMinVertexCover(const QueryPattern& query) const {
    std::unordered_set<VertexID> cover_set;

    std::vector<std::pair<VertexID, VertexID> > all_edges;
    all_edges.reserve(query.num_vertices * 4);

    std::unordered_set<uint64_t> seen;
    seen.reserve(query.num_vertices * 8 + 16);

    for (ui u = 0; u < query.num_vertices; ++u) {
        for (size_t i = 0; i < query.out_neighbors[u].size(); ++i) {
            const QueryNeighborInfo& e = query.out_neighbors[u][i];
            if (e.neighbor_id == u) {
                continue;
            }
            uint64_t key = packDirectedEdge(u, e.neighbor_id, e.relation_id);
            if (seen.insert(key).second) {
                all_edges.push_back(std::make_pair(u, e.neighbor_id));
            }
        }
    }

    std::vector<unsigned char> covered(all_edges.size(), 0);
    ui num_covered = 0;

    while (num_covered < all_edges.size()) {
        VertexID best_u = std::numeric_limits<VertexID>::max();
        int max_inc = -1;

        for (ui u = 0; u < query.num_vertices; ++u) {
            if (cover_set.count(u)) {
                continue;
            }
            int inc = 0;
            for (size_t i = 0; i < all_edges.size(); ++i) {
                if (!covered[i] && (all_edges[i].first == u || all_edges[i].second == u)) {
                    inc += 1;
                }
            }
            if (inc > max_inc) {
                max_inc = inc;
                best_u = u;
            }
        }

        if (best_u != std::numeric_limits<VertexID>::max() && max_inc > 0) {
            cover_set.insert(best_u);
            for (size_t i = 0; i < all_edges.size(); ++i) {
                if (!covered[i] && (all_edges[i].first == best_u || all_edges[i].second == best_u)) {
                    covered[i] = 1;
                    num_covered += 1;
                }
            }
        } else {
            break;
        }
    }

    if (log_mode_ == LogMode::Debug) {
        std::cerr << "[cover] |E|=" << all_edges.size()
                  << " |C|=" << cover_set.size() << std::endl;
    }
    return cover_set;
}

void VertexCoverRefiner::initializeCandidatesForCover(const QueryPattern& query,
                                                      const std::unordered_set<VertexID>& cover_set,
                                                      std::vector<std::vector<VertexID> >& candidates,
                                                      ui top_n,
                                                      const std::vector<const float*>& query_vertex_embeddings,
                                                      ui per_node_cap) const {
    last_cand_init_stats_ = CandInitStats();
    const ui kg_nodes = kg_.getVerticesCount();
    candidates.assign(query.num_vertices, std::vector<VertexID>());

    for (std::unordered_set<VertexID>::const_iterator it = cover_set.begin(); it != cover_set.end(); ++it) {
        VertexID u = *it;
        const ui q_deg = static_cast<ui>(query.out_neighbors[u].size() + query.in_neighbors[u].size());
        const std::vector<RelationDegreeRequirement> rel_degree_reqs =
            buildRelationDegreeRequirements(u, query);
        std::vector<VertexID>& cand = candidates[u];
        const ui hard_cap = per_node_cap == 0 ? std::numeric_limits<ui>::max() : per_node_cap;
        const ui target_n = top_n == 0 ? hard_cap : std::min<ui>(top_n, hard_cap);
        const ui search_k = std::min<ui>(target_n, kg_nodes);

        const float* q_emb = (u < query_vertex_embeddings.size()) ? query_vertex_embeddings[u] : NULL;
        uint64_t considered = 0;

        if (ann_ != NULL && q_emb != NULL && search_k > 0) {
            
            
            VamanaANN::SearchConfig search_cfg;
            search_cfg.use_graph_search = true;
            search_cfg.ef_search = VamanaANN::computeNaiveLsearch(target_n);
            std::vector<VamanaANN::SearchResult> ann_results = ann_->search(q_emb, search_k, search_cfg);
            considered = static_cast<uint64_t>(ann_results.size());
            last_cand_init_stats_.pool_size += considered;
            last_cand_init_stats_.sim_computations += considered;
            cand.reserve(ann_results.size());
            for (size_t i = 0; i < ann_results.size(); ++i) {
                VertexID v = ann_results[i].id;
                ui out_count = 0;
                ui in_count = 0;
                kg_.getOutNeighbors(v, out_count);
                kg_.getInNeighbors(v, in_count);
                if (out_count + in_count < q_deg) {
                    continue;
                }
                if (!relationDegreeOk(v, rel_degree_reqs)) {
                    continue;
                }
                last_cand_init_stats_.kept_by_degree += 1;
                cand.push_back(v);
                if (cand.size() >= hard_cap) {
                    break;
                }
            }
        } else if (target_n > 0) {
            
            
            last_cand_init_stats_.wildcard_nodes += 1;
            considered = static_cast<uint64_t>(kg_nodes);
            last_cand_init_stats_.pool_size += considered;
            for (VertexID v = 0; v < kg_nodes; ++v) {
                ui out_count = 0;
                ui in_count = 0;
                kg_.getOutNeighbors(v, out_count);
                kg_.getInNeighbors(v, in_count);
                if (out_count + in_count < q_deg) {
                    continue;
                }
                if (!relationDegreeOk(v, rel_degree_reqs)) {
                    continue;
                }
                last_cand_init_stats_.kept_by_degree += 1;
                cand.push_back(v);
                if (cand.size() >= hard_cap) {
                    break;
                }
            }
        }
        last_cand_init_stats_.kept_by_topn += static_cast<uint64_t>(cand.size());

        if (log_mode_ == LogMode::Debug) {
            std::cerr << "[cand-init] cover_node=" << u
                      << " vamana_topn"
                      << " pool=" << considered
                      << " -> cand=" << cand.size()
                      << " (target=" << target_n << ")" << std::endl;
        }
    }
}

std::vector<VertexCoverRefiner::DirectedConstraint> VertexCoverRefiner::buildCrossSetConstraints(
    VertexID query_node, const QueryPattern& query, const std::unordered_set<VertexID>& opposite_set) const {
    std::vector<DirectedConstraint> constraints;
    constraints.reserve(query.out_neighbors[query_node].size() + query.in_neighbors[query_node].size());

    for (size_t i = 0; i < query.out_neighbors[query_node].size(); ++i) {
        const QueryNeighborInfo& n = query.out_neighbors[query_node][i];
        if (opposite_set.count(n.neighbor_id)) {
            constraints.push_back(DirectedConstraint{n.neighbor_id, n.relation_id, true});
        }
    }
    for (size_t i = 0; i < query.in_neighbors[query_node].size(); ++i) {
        const QueryNeighborInfo& n = query.in_neighbors[query_node][i];
        if (opposite_set.count(n.neighbor_id)) {
            constraints.push_back(DirectedConstraint{n.neighbor_id, n.relation_id, false});
        }
    }
    return constraints;
}

void VertexCoverRefiner::iterativeRefinement(const QueryPattern& query,
                                             const std::unordered_set<VertexID>& cover_set,
                                             const std::unordered_set<VertexID>& independent_set,
                                             std::vector<std::vector<VertexID> >& candidates,
                                             ui max_iters,
                                             ui per_node_cap) const {
    last_refine_iters_ = 0;
    if (candidates.size() != query.num_vertices) {
        candidates.assign(query.num_vertices, std::vector<VertexID>());
    }
    const VertexID kg_nodes = kg_.getVerticesCount();
    std::vector<uint32_t> support_mark(kg_nodes, 0);
    std::vector<uint32_t> pool_mark(kg_nodes, 0);
    std::vector<uint32_t> inferred_mark(kg_nodes, 0);
    std::vector<std::vector<unsigned char> > degree_known(
        query.num_vertices, std::vector<unsigned char>(kg_nodes, 0));
    std::vector<std::vector<unsigned char> > degree_ok_cache(
        query.num_vertices, std::vector<unsigned char>(kg_nodes, 0));
    uint32_t support_stamp = 1;
    uint32_t pool_stamp = 1;
    uint32_t inferred_stamp = 1;

    auto next_stamp = [](uint32_t& stamp, std::vector<uint32_t>& marks) {
        stamp += 1;
        if (stamp == 0) {
            std::fill(marks.begin(), marks.end(), 0);
            stamp = 1;
        }
    };
    auto cached_relation_degree_ok = [&](VertexID query_node,
                                         VertexID kg_v,
                                         const std::vector<RelationDegreeRequirement>& requirements) -> bool {
        if (kg_v >= kg_nodes) {
            return false;
        }
        if (degree_known[query_node][kg_v]) {
            return degree_ok_cache[query_node][kg_v] != 0;
        }
        bool ok = relationDegreeOk(kg_v, requirements);
        degree_known[query_node][kg_v] = 1;
        degree_ok_cache[query_node][kg_v] = ok ? 1 : 0;
        return ok;
    };

    auto infer_by_constraints = [&](VertexID query_node,
                                    const std::vector<DirectedConstraint>& constraints,
                                    const std::vector<std::vector<VertexID> >& cur_candidates,
                                    std::vector<VertexID>& out_pool) {
        out_pool.clear();
        bool first = true;
        const std::vector<RelationDegreeRequirement> rel_degree_reqs =
            buildRelationDegreeRequirements(query_node, query);
        std::vector<VertexID> support_list;
        std::vector<VertexID> inter;

        for (size_t c = 0; c < constraints.size(); ++c) {
            const DirectedConstraint& cons = constraints[c];
            next_stamp(support_stamp, support_mark);
            support_list.clear();
            support_list.reserve(cur_candidates[cons.neighbor].size() * 4 + 16);

            
            
            
            auto consume_edges = [&](VertexID v_n, const VertexID* nbs, const ui* eids,
                                     ui edge_count, bool ) {
                for (ui j = 0; j < edge_count; ++j) {
                    if (!relationMatch(cons.relation_id, kg_.getEdgeRelationId(eids[j]))) {
                        continue;
                    }
                    if (!labelOk(query_node, nbs[j])) {
                        continue;
                    }
                    if (!cached_relation_degree_ok(query_node, nbs[j], rel_degree_reqs)) {
                        continue;
                    }
                    VertexID supported = nbs[j];
                    if (supported < kg_nodes && support_mark[supported] != support_stamp) {
                        support_mark[supported] = support_stamp;
                        support_list.push_back(supported);
                    }
                }
                (void)v_n;
            };

            for (size_t i = 0; i < cur_candidates[cons.neighbor].size(); ++i) {
                VertexID v_n = cur_candidates[cons.neighbor][i];

                if (undirected_mode_) {
                    ui in_count = 0;
                    const VertexID* in_nbs = kg_.getInNeighbors(v_n, in_count);
                    const ui* in_eids = kg_.getInEdgeIds(v_n, in_count);
                    consume_edges(v_n, in_nbs, in_eids, in_count, false);
                    ui out_count = 0;
                    const VertexID* out_nbs = kg_.getOutNeighbors(v_n, out_count);
                    const ui* out_eids = kg_.getOutEdgeIds(v_n, out_count);
                    consume_edges(v_n, out_nbs, out_eids, out_count, true);
                } else {
                    ui edge_count = 0;
                    const VertexID* nbs = NULL;
                    const ui* eids = NULL;
                    if (cons.is_outgoing) {
                        
                        nbs = kg_.getInNeighbors(v_n, edge_count);
                        eids = kg_.getInEdgeIds(v_n, edge_count);
                    } else {
                        
                        nbs = kg_.getOutNeighbors(v_n, edge_count);
                        eids = kg_.getOutEdgeIds(v_n, edge_count);
                    }
                    consume_edges(v_n, nbs, eids, edge_count, cons.is_outgoing);
                }
            }

            if (first) {
                out_pool.swap(support_list);
                next_stamp(pool_stamp, pool_mark);
                for (size_t i = 0; i < out_pool.size(); ++i) {
                    if (out_pool[i] < kg_nodes) {
                        pool_mark[out_pool[i]] = pool_stamp;
                    }
                }
                first = false;
            } else {
                inter.clear();
                inter.reserve(std::min(out_pool.size(), support_list.size()));
                if (support_list.size() < out_pool.size()) {
                    for (size_t si = 0; si < support_list.size(); ++si) {
                        VertexID v = support_list[si];
                        if (v < kg_nodes && pool_mark[v] == pool_stamp) {
                            inter.push_back(v);
                        }
                    }
                } else {
                    for (size_t pi = 0; pi < out_pool.size(); ++pi) {
                        VertexID v = out_pool[pi];
                        if (v < kg_nodes && support_mark[v] == support_stamp) {
                            inter.push_back(v);
                        }
                    }
                }
                out_pool.swap(inter);
                next_stamp(pool_stamp, pool_mark);
                for (size_t i = 0; i < out_pool.size(); ++i) {
                    if (out_pool[i] < kg_nodes) {
                        pool_mark[out_pool[i]] = pool_stamp;
                    }
                }
            }

            if (!first && out_pool.empty()) {
                break;
            }
        }

        if (log_mode_ == LogMode::Debug) {
            std::cerr << "[refine] infer query_node=" << query_node
                      << " constraints=" << constraints.size()
                      << " inferred=" << out_pool.size() << std::endl;
        }
    };

    auto intersect_with_inferred = [&](const std::vector<VertexID>& current,
                                       const std::vector<VertexID>& inferred,
                                       std::vector<VertexID>& output) {
        output.clear();
        if (inferred.empty()) {
            return;
        }
        next_stamp(inferred_stamp, inferred_mark);
        for (size_t i = 0; i < inferred.size(); ++i) {
            if (inferred[i] < kg_nodes) {
                inferred_mark[inferred[i]] = inferred_stamp;
            }
        }
        output.reserve(std::min(current.size(), inferred.size()));
        for (size_t i = 0; i < current.size(); ++i) {
            if (current[i] < kg_nodes && inferred_mark[current[i]] == inferred_stamp) {
                output.push_back(current[i]);
            }
        }
    };

    
    for (std::unordered_set<VertexID>::const_iterator it_d = independent_set.begin();
         it_d != independent_set.end(); ++it_d) {
        VertexID u_d = *it_d;
        std::vector<DirectedConstraint> constraints = buildCrossSetConstraints(u_d, query, cover_set);
        if (constraints.empty()) {
            continue;
        }
        std::vector<VertexID> pool;
        infer_by_constraints(u_d, constraints, candidates, pool);

        candidates[u_d].clear();
        candidates[u_d].reserve(pool.size());
        for (size_t pi = 0; pi < pool.size(); ++pi) {
            candidates[u_d].push_back(pool[pi]);
        }
        if (per_node_cap > 0 && candidates[u_d].size() > per_node_cap) {
            candidates[u_d].resize(per_node_cap);
        }
    }

    bool changed = true;
    ui iter = 0;
    while (changed && iter < max_iters) {
        changed = false;
        iter += 1;

        
        for (std::unordered_set<VertexID>::const_iterator it_c = cover_set.begin();
             it_c != cover_set.end(); ++it_c) {
            VertexID u_c = *it_c;
            std::vector<DirectedConstraint> constraints = buildCrossSetConstraints(u_c, query, independent_set);
            if (constraints.empty()) {
                continue;
            }

            std::vector<VertexID> inferred_pool;
            infer_by_constraints(u_c, constraints, candidates, inferred_pool);

            std::vector<VertexID> next_cands;
            intersect_with_inferred(candidates[u_c], inferred_pool, next_cands);

            if (next_cands.size() != candidates[u_c].size()) {
                changed = true;
            }
            candidates[u_c].swap(next_cands);
        }

        
        for (std::unordered_set<VertexID>::const_iterator it_d = independent_set.begin();
             it_d != independent_set.end(); ++it_d) {
            VertexID u_d = *it_d;
            std::vector<DirectedConstraint> constraints = buildCrossSetConstraints(u_d, query, cover_set);
            if (constraints.empty()) {
                continue;
            }

            std::vector<VertexID> inferred_pool;
            infer_by_constraints(u_d, constraints, candidates, inferred_pool);

            std::vector<VertexID> next_cands;
            intersect_with_inferred(candidates[u_d], inferred_pool, next_cands);

            if (next_cands.size() != candidates[u_d].size()) {
                changed = true;
            }
            candidates[u_d].swap(next_cands);
        }

        if (log_mode_ == LogMode::Debug) {
            size_t sum_c = 0;
            size_t sum_d = 0;
            for (std::unordered_set<VertexID>::const_iterator it_c = cover_set.begin();
                 it_c != cover_set.end(); ++it_c) {
                sum_c += candidates[*it_c].size();
            }
            for (std::unordered_set<VertexID>::const_iterator it_d = independent_set.begin();
                 it_d != independent_set.end(); ++it_d) {
                sum_d += candidates[*it_d].size();
            }
            std::cerr << "[refine] iter=" << iter
                      << " changed=" << (changed ? 1 : 0)
                      << " sumCandC=" << sum_c
                      << " sumCandD=" << sum_d << std::endl;
        }
    }
    last_refine_iters_ = iter;
}
