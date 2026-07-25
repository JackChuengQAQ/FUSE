#include "upper_bound_pruning.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <string>

UpperBoundPruner::UpperBoundPruner(const EmbeddingDiGraph& kg, bool verbose)
    : kg_(kg),
      verbose_(verbose),
      undirected_mode_(false) {
}

void UpperBoundPruner::setRelationNameMap(const std::vector<ui>& rel_q2k) {
    rel_q2k_ = rel_q2k;
}

void UpperBoundPruner::setUndirectedMode(bool enabled) {
    undirected_mode_ = enabled;
}

void UpperBoundPruner::setLowerBoundEnabled(bool enabled) {
    lb_enabled_ = enabled;
}

bool UpperBoundPruner::relationMatch(ui query_rel, ui kg_rel) const {
    if (query_rel >= rel_q2k_.size()) {
        return false;
    }
    ui mapped = rel_q2k_[query_rel];
    if (mapped == std::numeric_limits<ui>::max()) {
        return false;
    }
    return mapped == kg_rel;
}

VertexID UpperBoundPruner::selectRootByMaxDegree(const QueryPattern& query) const {
    VertexID root = 0;
    ui best_deg = 0;
    for (ui u = 0; u < query.num_vertices; ++u) {
        ui deg = static_cast<ui>(query.out_neighbors[u].size() + query.in_neighbors[u].size());
        if (deg > best_deg) {
            best_deg = deg;
            root = u;
        }
    }
    return root;
}

VertexID UpperBoundPruner::selectRootByCandidateSet(
    const QueryPattern& query,
    const std::vector<std::vector<VertexID> >& candidates) const {
    VertexID root = 0;
    size_t best_size = std::numeric_limits<size_t>::max();
    ui best_deg = 0;
    for (ui u = 0; u < query.num_vertices; ++u) {
        size_t cand_size = (u < candidates.size()) ? candidates[u].size() : 0;
        ui deg = static_cast<ui>(query.out_neighbors[u].size() + query.in_neighbors[u].size());
        if (cand_size < best_size || (cand_size == best_size && deg > best_deg)) {
            best_size = cand_size;
            best_deg = deg;
            root = u;
        }
    }
    return root;
}

void UpperBoundPruner::buildDfsAndDag(const QueryPattern& query,
                                      VertexID root,
                                      std::vector<VertexID>& bfs_order,
                                      std::vector<std::vector<DagEdge> >& dag_children) const {
    bfs_order.clear();
    dag_children.assign(query.num_vertices, std::vector<DagEdge>());

    std::vector<unsigned char> visited(query.num_vertices, 0);

    bool use_bfs_traversal = false;
    const char* traversal_env = std::getenv("FUSE_TRAVERSAL");
    if (traversal_env != NULL) {
        std::string mode(traversal_env);
        if (mode == "bfs" || mode == "BFS") {
            use_bfs_traversal = true;
        }
    }

    if (use_bfs_traversal) {
        std::queue<VertexID> q;
        auto start_component_bfs = [&](VertexID s) {
            visited[s] = 1;
            bfs_order.push_back(s);
            q.push(s);
        };

        if (root < query.num_vertices) {
            start_component_bfs(root);
        }

        while (!q.empty()) {
            VertexID u = q.front();
            q.pop();

            for (size_t i = 0; i < query.out_neighbors[u].size(); ++i) {
                VertexID v = query.out_neighbors[u][i].neighbor_id;
                if (!visited[v]) {
                    visited[v] = 1;
                    bfs_order.push_back(v);
                    dag_children[u].push_back(DagEdge{v, query.out_neighbors[u][i].relation_id, false});
                    q.push(v);
                }
            }
            for (size_t i = 0; i < query.in_neighbors[u].size(); ++i) {
                VertexID v = query.in_neighbors[u][i].neighbor_id;
                if (!visited[v]) {
                    visited[v] = 1;
                    bfs_order.push_back(v);
                    dag_children[u].push_back(DagEdge{v, query.in_neighbors[u][i].relation_id, true});
                    q.push(v);
                }
            }
        }

        for (ui u = 0; u < query.num_vertices; ++u) {
            if (!visited[u]) {
                start_component_bfs(u);
                while (!q.empty()) {
                    VertexID x = q.front();
                    q.pop();
                    for (size_t i = 0; i < query.out_neighbors[x].size(); ++i) {
                        VertexID y = query.out_neighbors[x][i].neighbor_id;
                        if (!visited[y]) {
                            visited[y] = 1;
                            bfs_order.push_back(y);
                            dag_children[x].push_back(DagEdge{y, query.out_neighbors[x][i].relation_id, false});
                            q.push(y);
                        }
                    }
                    for (size_t i = 0; i < query.in_neighbors[x].size(); ++i) {
                        VertexID y = query.in_neighbors[x][i].neighbor_id;
                        if (!visited[y]) {
                            visited[y] = 1;
                            bfs_order.push_back(y);
                            dag_children[x].push_back(DagEdge{y, query.in_neighbors[x][i].relation_id, true});
                            q.push(y);
                        }
                    }
                }
            }
        }
        return;
    }

    std::function<void(VertexID)> dfs_visit = [&](VertexID u) {
        for (size_t i = 0; i < query.out_neighbors[u].size(); ++i) {
            VertexID v = query.out_neighbors[u][i].neighbor_id;
            if (!visited[v]) {
                visited[v] = 1;
                bfs_order.push_back(v);
                dag_children[u].push_back(DagEdge{v, query.out_neighbors[u][i].relation_id, false});
                dfs_visit(v);
            }
        }
        for (size_t i = 0; i < query.in_neighbors[u].size(); ++i) {
            VertexID v = query.in_neighbors[u][i].neighbor_id;
            if (!visited[v]) {
                visited[v] = 1;
                bfs_order.push_back(v);
                dag_children[u].push_back(DagEdge{v, query.in_neighbors[u][i].relation_id, true});
                dfs_visit(v);
            }
        }
    };

    auto start_component = [&](VertexID s) {
        visited[s] = 1;
        bfs_order.push_back(s);
        dfs_visit(s);
    };

    if (root < query.num_vertices) {
        start_component(root);
    }

    for (ui u = 0; u < query.num_vertices; ++u) {
        if (!visited[u]) {
            start_component(u);
        }
    }
}

float UpperBoundPruner::nodeSimilarity(VertexID query_u, const float* query_emb, VertexID kg_v) const {
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
    (void)query_u;
    return -std::sqrt(dist2);
}

void UpperBoundPruner::computeUpperBounds(
    const QueryPattern& query,
    const std::vector<const float*>& query_vertex_embeddings,
    const std::vector<VertexID>& bfs_order,
    const std::vector<std::vector<DagEdge> >& dag_children,
    const std::vector<std::vector<VertexID> >& candidates,
    std::vector<std::unordered_map<VertexID, float> >& ub_table) const {
    ub_table.clear();
    ub_table.resize(query.num_vertices);

    struct UbProfile {
        double total_ms = 0.0;
        double mark_build_ms = 0.0;
        double similarity_ms = 0.0;
        double child_scan_ms = 0.0;
        double store_ms = 0.0;
        uint64_t candidate_cells = 0;
        uint64_t similarity_calls = 0;
        uint64_t child_edges_scanned = 0;
        uint64_t child_candidate_hits = 0;
        uint64_t child_ub_hits = 0;
        uint64_t child_rel_hits = 0;
        uint64_t ub_kept = 0;
    };
    UbProfile profile;
    auto debug_now = []() -> std::chrono::high_resolution_clock::time_point {
        return std::chrono::high_resolution_clock::now();
    };
    auto debug_elapsed_ms = [](const std::chrono::high_resolution_clock::time_point& t0) -> double {
        const std::chrono::high_resolution_clock::time_point t1 =
            std::chrono::high_resolution_clock::now();
        return static_cast<double>(
                   std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) /
               1000.0;
    };
    std::chrono::high_resolution_clock::time_point profile_total_t0;
    if (verbose_) {
        profile_total_t0 = debug_now();
    }

    const VertexID kg_vertices = kg_.getVerticesCount();
    const float neg_inf = -std::numeric_limits<float>::max();

    std::chrono::high_resolution_clock::time_point mark_t0;
    if (verbose_) {
        mark_t0 = debug_now();
    }
    std::vector<std::vector<unsigned char> > candidate_marks(
        query.num_vertices, std::vector<unsigned char>(kg_vertices, 0));
    std::vector<std::vector<float> > ub_values(
        query.num_vertices, std::vector<float>(kg_vertices, neg_inf));
    for (ui u = 0; u < query.num_vertices; ++u) {
        if (verbose_) {
            profile.candidate_cells += candidates[u].size();
        }
        for (size_t i = 0; i < candidates[u].size(); ++i) {
            VertexID v = candidates[u][i];
            if (v < kg_vertices) {
                candidate_marks[u][v] = 1;
            }
        }
    }
    if (verbose_) {
        profile.mark_build_ms = debug_elapsed_ms(mark_t0);
    }

    VertexID root = bfs_order.empty() ? std::numeric_limits<VertexID>::max() : bfs_order[0];
    if (verbose_) {
        std::cerr << "[ub-debug] computeUpperBounds begin root=" << root << std::endl;
    }

    std::vector<VertexID> reverse_bfs = bfs_order;
    std::reverse(reverse_bfs.begin(), reverse_bfs.end());

    for (size_t ord = 0; ord < reverse_bfs.size(); ++ord) {
        VertexID u = reverse_bfs[ord];
        const float* q_emb = (u < query_vertex_embeddings.size() ? query_vertex_embeddings[u] : NULL);
        std::chrono::high_resolution_clock::time_point node_t0;
        if (verbose_) {
            node_t0 = std::chrono::high_resolution_clock::now();
        }

        for (size_t ci = 0; ci < candidates[u].size(); ++ci) {
            VertexID v = candidates[u][ci];
            std::chrono::high_resolution_clock::time_point sim_t0;
            if (verbose_) {
                sim_t0 = debug_now();
            }
            float base = nodeSimilarity(u, q_emb, v);
            if (verbose_) {
                profile.similarity_ms += debug_elapsed_ms(sim_t0);
                profile.similarity_calls += 1;
            }

            bool feasible = true;
            float children_best_sum = 0.0f;
            if (verbose_ && u == root) {
                std::cerr << "[ub-debug] root-cand id=" << v
                          << " name=\"" << kg_.getVertexName(v) << "\""
                          << " base=" << base << std::endl;
            }

            for (size_t e = 0; e < dag_children[u].size(); ++e) {
                const DagEdge& edge = dag_children[u][e];
                VertexID child = edge.child;
                ui count_neighbor_hit = 0;
                ui count_with_ub = 0;
                ui count_rel_ok = 0;
                float best_single = neg_inf;

                auto consume_one_direction = [&](const VertexID* nbs, const ui* eids, ui edge_count) {
                    if (verbose_) {
                        profile.child_edges_scanned += edge_count;
                    }
                    for (ui j = 0; j < edge_count; ++j) {
                        VertexID v_child = nbs[j];
                        if (v_child >= kg_vertices || !candidate_marks[child][v_child]) {
                            continue;
                        }
                        count_neighbor_hit += 1;
                        if (verbose_) {
                            profile.child_candidate_hits += 1;
                        }

                        float value = ub_values[child][v_child];
                        if (value <= neg_inf / 2) {
                            continue;
                        }
                        count_with_ub += 1;
                        if (verbose_) {
                            profile.child_ub_hits += 1;
                        }

                        
                        if (!relationMatch(edge.relation_id, kg_.getEdgeRelationId(eids[j]))) {
                            continue;
                        }
                        count_rel_ok += 1;
                        if (verbose_) {
                            profile.child_rel_hits += 1;
                        }

                        if (value > best_single) {
                            best_single = value;
                        }
                    }
                };

                std::chrono::high_resolution_clock::time_point scan_t0;
                if (verbose_) {
                    scan_t0 = debug_now();
                }
                if (edge.query_edge_reversed) {
                    ui in_count = 0;
                    const VertexID* in_nbs = kg_.getInNeighbors(v, in_count);
                    const ui* in_eids = kg_.getInEdgeIds(v, in_count);
                    consume_one_direction(in_nbs, in_eids, in_count);
                } else {
                    ui out_count = 0;
                    const VertexID* out_nbs = kg_.getOutNeighbors(v, out_count);
                    const ui* out_eids = kg_.getOutEdgeIds(v, out_count);
                    consume_one_direction(out_nbs, out_eids, out_count);
                }
                if (undirected_mode_) {
                    if (edge.query_edge_reversed) {
                        ui out_count = 0;
                        const VertexID* out_nbs = kg_.getOutNeighbors(v, out_count);
                        const ui* out_eids = kg_.getOutEdgeIds(v, out_count);
                        consume_one_direction(out_nbs, out_eids, out_count);
                    } else {
                        ui in_count = 0;
                        const VertexID* in_nbs = kg_.getInNeighbors(v, in_count);
                        const ui* in_eids = kg_.getInEdgeIds(v, in_count);
                        consume_one_direction(in_nbs, in_eids, in_count);
                    }
                }
                if (verbose_) {
                    profile.child_scan_ms += debug_elapsed_ms(scan_t0);
                }

                if (best_single <= neg_inf / 2) {
                    feasible = false;
                    if (verbose_ && u == root) {
                        std::cerr << "[ub-debug]   child=" << child
                                  << " rel_q=" << edge.relation_id
                                  << " neighbor_hit=" << count_neighbor_hit
                                  << " with_child_ub=" << count_with_ub
                                  << " rel_ok=" << count_rel_ok
                                  << " => FAIL" << std::endl;
                    }
                    break;
                }
                children_best_sum += best_single;

                if (verbose_ && u == root) {
                    std::cerr << "[ub-debug]   child=" << child
                              << " rel_q=" << edge.relation_id
                              << " rev=" << (edge.query_edge_reversed ? 1 : 0)
                              << " neighbor_hit=" << count_neighbor_hit
                              << " with_child_ub=" << count_with_ub
                              << " rel_ok=" << count_rel_ok
                              << " best_child=" << best_single << std::endl;
                }
            }

            if (feasible) {
                float ub_value = base + children_best_sum;
                std::chrono::high_resolution_clock::time_point store_t0;
                if (verbose_) {
                    store_t0 = debug_now();
                }
                ub_table[u][v] = ub_value;
                if (v < kg_vertices) {
                    ub_values[u][v] = ub_value;
                }
                if (verbose_) {
                    profile.store_ms += debug_elapsed_ms(store_t0);
                    profile.ub_kept += 1;
                }
                if (verbose_ && u == root) {
                    std::cerr << "[ub-debug] root-cand KEEP id=" << v
                      << " ub=" << ub_table[u][v] << std::endl;
                }
            } else if (verbose_ && u == root) {
                std::cerr << "[ub-debug] root-cand DROP id=" << v << std::endl;
            }
        }

        if (verbose_) {
            double node_ms = 0.0;
            std::chrono::high_resolution_clock::time_point node_t1 =
                std::chrono::high_resolution_clock::now();
            node_ms = static_cast<double>(
                          std::chrono::duration_cast<std::chrono::microseconds>(node_t1 - node_t0).count()) /
                      1000.0;
            std::cerr << "[ub-debug] node=" << u
                      << " cand_in=" << candidates[u].size()
                      << " ub_kept=" << ub_table[u].size()
                      << " ub_ms=" << node_ms << std::endl;
        }
    }

    if (verbose_) {
        profile.total_ms = debug_elapsed_ms(profile_total_t0);
        double accounted_ms = profile.mark_build_ms + profile.similarity_ms +
                              profile.child_scan_ms + profile.store_ms;
        double other_ms = profile.total_ms - accounted_ms;
        std::cerr << "[ub-profile] total_ms=" << profile.total_ms
                  << " mark_build_ms=" << profile.mark_build_ms
                  << " similarity_ms=" << profile.similarity_ms
                  << " child_scan_ms=" << profile.child_scan_ms
                  << " store_ms=" << profile.store_ms
                  << " other_ms=" << other_ms
                  << " candidate_cells=" << profile.candidate_cells
                  << " similarity_calls=" << profile.similarity_calls
                  << " child_edges_scanned=" << profile.child_edges_scanned
                  << " child_candidate_hits=" << profile.child_candidate_hits
                  << " child_ub_hits=" << profile.child_ub_hits
                  << " child_rel_hits=" << profile.child_rel_hits
                  << " ub_kept=" << profile.ub_kept
                  << std::endl;
    }
}

void UpperBoundPruner::pruneByRootTopKAndDescendants(
    const QueryPattern& query,
    const std::vector<VertexID>& bfs_order,
    const std::vector<std::vector<DagEdge> >& dag_children,
    ui root_top_k,
    std::vector<std::vector<VertexID> >& candidates,
    std::vector<std::unordered_map<VertexID, float> >& ub_table) const {
    if (bfs_order.empty() || root_top_k == 0) {
        return;
    }

    VertexID root = bfs_order[0];
    std::vector<std::pair<VertexID, float> > root_scored;
    root_scored.reserve(ub_table[root].size());
    for (std::unordered_map<VertexID, float>::const_iterator it = ub_table[root].begin();
         it != ub_table[root].end(); ++it) {
        root_scored.push_back(std::make_pair(it->first, it->second));
    }

    std::sort(root_scored.begin(), root_scored.end(),
              [](const std::pair<VertexID, float>& a, const std::pair<VertexID, float>& b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
              });

    if (verbose_) {
        std::cerr << "[ub-debug] root_scored_before_topk=" << root_scored.size() << std::endl;
        const size_t print_n = std::min<size_t>(root_scored.size(), 20);
        for (size_t i = 0; i < print_n; ++i) {
            std::cerr << "[ub-debug] root_rank=" << (i + 1)
                      << " id=" << root_scored[i].first
                      << " score=" << root_scored[i].second
                      << " name=\"" << kg_.getVertexName(root_scored[i].first) << "\""
                      << std::endl;
        }
    }

    if (root_scored.size() > root_top_k) {
        root_scored.resize(root_top_k);
    }

    candidates[root].clear();
    ub_table[root].clear();
    std::unordered_set<VertexID> kept_root;
    kept_root.reserve(root_scored.size() * 2 + 16);
    for (size_t i = 0; i < root_scored.size(); ++i) {
        candidates[root].push_back(root_scored[i].first);
        ub_table[root][root_scored[i].first] = root_scored[i].second;
        kept_root.insert(root_scored[i].first);
    }

    
    std::vector<std::unordered_set<VertexID> > candidate_set(query.num_vertices);
    for (ui u = 0; u < query.num_vertices; ++u) {
        candidate_set[u].reserve(candidates[u].size() * 2 + 16);
        for (size_t i = 0; i < candidates[u].size(); ++i) {
            candidate_set[u].insert(candidates[u][i]);
        }
    }

    std::vector<std::unordered_set<VertexID> > allowed(query.num_vertices);
    std::vector<unsigned char> initialized(query.num_vertices, 0);
    allowed[root] = kept_root;
    initialized[root] = 1;

    for (size_t bi = 0; bi < bfs_order.size(); ++bi) {
        VertexID u = bfs_order[bi];
        if (!initialized[u]) {
            continue;
        }
        for (size_t e = 0; e < dag_children[u].size(); ++e) {
            const DagEdge& edge = dag_children[u][e];
            VertexID child = edge.child;
            ui rel_q = edge.relation_id;

            std::unordered_map<VertexID, float> support_best;
            support_best.reserve(candidate_set[child].size() * 2 + 16);

            for (std::unordered_set<VertexID>::const_iterator pit = allowed[u].begin();
                 pit != allowed[u].end(); ++pit) {
                VertexID v_parent = *pit;
                auto collect_support_one_direction = [&](const VertexID* nbs, const ui* eids, ui edge_count) {
                    for (ui j = 0; j < edge_count; ++j) {
                        VertexID v_child = nbs[j];
                        if (!candidate_set[child].count(v_child)) {
                            continue;
                        }
                        if (ub_table[child].find(v_child) == ub_table[child].end()) {
                            continue;
                        }
                        if (!relationMatch(rel_q, kg_.getEdgeRelationId(eids[j]))) {
                            continue;
                        }
                        float support_score = ub_table[child][v_child];
                        std::unordered_map<VertexID, float>::iterator it_best = support_best.find(v_child);
                        if (it_best == support_best.end() || support_score > it_best->second) {
                            support_best[v_child] = support_score;
                        }
                    }
                };

                if (edge.query_edge_reversed) {
                    ui in_count = 0;
                    const VertexID* in_nbs = kg_.getInNeighbors(v_parent, in_count);
                    const ui* in_eids = kg_.getInEdgeIds(v_parent, in_count);
                    collect_support_one_direction(in_nbs, in_eids, in_count);
                } else {
                    ui out_count = 0;
                    const VertexID* out_nbs = kg_.getOutNeighbors(v_parent, out_count);
                    const ui* out_eids = kg_.getOutEdgeIds(v_parent, out_count);
                    collect_support_one_direction(out_nbs, out_eids, out_count);
                }
                if (undirected_mode_) {
                    if (edge.query_edge_reversed) {
                        ui out_count = 0;
                        const VertexID* out_nbs = kg_.getOutNeighbors(v_parent, out_count);
                        const ui* out_eids = kg_.getOutEdgeIds(v_parent, out_count);
                        collect_support_one_direction(out_nbs, out_eids, out_count);
                    } else {
                        ui in_count = 0;
                        const VertexID* in_nbs = kg_.getInNeighbors(v_parent, in_count);
                        const ui* in_eids = kg_.getInEdgeIds(v_parent, in_count);
                        collect_support_one_direction(in_nbs, in_eids, in_count);
                    }
                }
            }

            if (!initialized[child]) {
                allowed[child].clear();
                allowed[child].reserve(support_best.size() * 2 + 16);
                for (std::unordered_map<VertexID, float>::const_iterator sit = support_best.begin();
                     sit != support_best.end(); ++sit) {
                    allowed[child].insert(sit->first);
                    ub_table[child][sit->first] = sit->second;
                }
                initialized[child] = 1;
            } else {
                std::unordered_set<VertexID> inter;
                inter.reserve(std::min(allowed[child].size(), support_best.size()) * 2 + 16);
                if (support_best.size() < allowed[child].size()) {
                    for (std::unordered_map<VertexID, float>::const_iterator sit = support_best.begin();
                         sit != support_best.end(); ++sit) {
                        if (allowed[child].count(sit->first)) {
                            inter.insert(sit->first);
                        }
                    }
                } else {
                    for (std::unordered_set<VertexID>::const_iterator ait = allowed[child].begin();
                         ait != allowed[child].end(); ++ait) {
                        if (support_best.count(*ait)) {
                            inter.insert(*ait);
                        }
                    }
                }
                allowed[child].swap(inter);

                
                for (std::unordered_set<VertexID>::const_iterator ait = allowed[child].begin();
                     ait != allowed[child].end(); ++ait) {
                    VertexID v_child = *ait;
                    std::unordered_map<VertexID, float>::const_iterator it_sup = support_best.find(v_child);
                    if (it_sup == support_best.end()) {
                        continue;
                    }
                    std::unordered_map<VertexID, float>::iterator it_ub = ub_table[child].find(v_child);
                    if (it_ub == ub_table[child].end()) {
                        ub_table[child][v_child] = it_sup->second;
                    } else if (it_sup->second < it_ub->second) {
                        it_ub->second = it_sup->second;
                    }
                }
            }
            if (verbose_) {
                std::cerr << "[ub-debug] propagate parent=" << u
                          << " child=" << child
                          << " parent_allowed=" << allowed[u].size()
                          << " support=" << support_best.size()
                          << " child_allowed_now=" << allowed[child].size()
                          << std::endl;
            }
        }
    }

    for (ui u = 0; u < query.num_vertices; ++u) {
        if (!initialized[u]) {
            continue;
        }
        std::vector<VertexID> next;
        next.reserve(candidates[u].size());
        std::unordered_map<VertexID, float> next_ub;
        next_ub.reserve(ub_table[u].size());
        for (size_t i = 0; i < candidates[u].size(); ++i) {
            VertexID v = candidates[u][i];
            if (allowed[u].count(v) && ub_table[u].find(v) != ub_table[u].end()) {
                next.push_back(v);
                next_ub[v] = ub_table[u][v];
            }
        }
        candidates[u].swap(next);
        ub_table[u].swap(next_ub);
        if (verbose_) {
            std::cerr << "[ub-debug] final node=" << u
                      << " cand_after_prune=" << candidates[u].size() << std::endl;
        }
    }

    if (verbose_) {
        std::cerr << "[ub] root=" << root
                  << " kept_root=" << candidates[root].size()
                  << " topk=" << root_top_k << std::endl;
    }
}

void UpperBoundPruner::packSupportedCandidates(
    const QueryPattern& query,
    const std::vector<const float*>& query_vertex_embeddings,
    const std::vector<VertexID>& bfs_order,
    const std::vector<std::vector<DagEdge> >& dag_children,
    ui top_k,
    std::vector<std::vector<VertexID> >& candidates,
    std::vector<std::unordered_map<VertexID, float> >& ub_table) const {
    if (top_k == 0 || candidates.size() != query.num_vertices || ub_table.size() != query.num_vertices) {
        return;
    }

    struct ParentEdge {
        VertexID parent;
        ui relation_id;
        bool query_edge_reversed;
    };
    std::vector<std::vector<ParentEdge> > parents(query.num_vertices);
    for (ui p = 0; p < dag_children.size(); ++p) {
        for (size_t ei = 0; ei < dag_children[p].size(); ++ei) {
            const DagEdge& edge = dag_children[p][ei];
            if (edge.child < query.num_vertices) {
                parents[edge.child].push_back(ParentEdge{p, edge.relation_id, edge.query_edge_reversed});
            }
        }
    }

    struct LeafGroupKey {
        VertexID parent;
        ui relation_id;
        bool query_edge_reversed;
    };
    struct LeafGroupCount {
        LeafGroupKey key;
        size_t count;
    };
    std::vector<LeafGroupCount> leaf_groups;
    std::vector<size_t> leaf_group_size(query.num_vertices, 1);
    for (VertexID child = 0; child < query.num_vertices; ++child) {
        const ui child_deg = static_cast<ui>(query.out_neighbors[child].size() + query.in_neighbors[child].size());
        if (child_deg != 1 || parents[child].size() != 1) {
            continue;
        }
        LeafGroupKey key{parents[child][0].parent,
                         parents[child][0].relation_id,
                         undirected_mode_ ? false : parents[child][0].query_edge_reversed};
        bool found = false;
        for (size_t gi = 0; gi < leaf_groups.size(); ++gi) {
            if (leaf_groups[gi].key.parent == key.parent &&
                leaf_groups[gi].key.relation_id == key.relation_id &&
                leaf_groups[gi].key.query_edge_reversed == key.query_edge_reversed) {
                leaf_groups[gi].count += 1;
                found = true;
                break;
            }
        }
        if (!found) {
            LeafGroupCount group;
            group.key = key;
            group.count = 1;
            leaf_groups.push_back(group);
        }
    }
    for (VertexID child = 0; child < query.num_vertices; ++child) {
        const ui child_deg = static_cast<ui>(query.out_neighbors[child].size() + query.in_neighbors[child].size());
        if (child_deg != 1 || parents[child].size() != 1) {
            continue;
        }
        LeafGroupKey key{parents[child][0].parent,
                         parents[child][0].relation_id,
                         undirected_mode_ ? false : parents[child][0].query_edge_reversed};
        for (size_t gi = 0; gi < leaf_groups.size(); ++gi) {
            if (leaf_groups[gi].key.parent == key.parent &&
                leaf_groups[gi].key.relation_id == key.relation_id &&
                leaf_groups[gi].key.query_edge_reversed == key.query_edge_reversed) {
                leaf_group_size[child] = leaf_groups[gi].count;
                break;
            }
        }
    }

    std::vector<std::unordered_set<VertexID> > candidate_sets(query.num_vertices);
    for (ui u = 0; u < query.num_vertices; ++u) {
        candidate_sets[u].reserve(candidates[u].size() * 2 + 16);
        for (size_t i = 0; i < candidates[u].size(); ++i) {
            if (ub_table[u].find(candidates[u][i]) != ub_table[u].end()) {
                candidate_sets[u].insert(candidates[u][i]);
            }
        }
    }

    auto collect_support = [&](const ParentEdge& pe,
                               VertexID child,
                               std::unordered_map<VertexID, float>& support_best) {
        support_best.clear();
        if (pe.parent >= candidates.size() || child >= candidates.size()) {
            return;
        }
        support_best.reserve(candidate_sets[child].size() * 2 + 16);

        for (size_t pi = 0; pi < candidates[pe.parent].size(); ++pi) {
            VertexID v_parent = candidates[pe.parent][pi];
            auto collect_one_direction = [&](const VertexID* nbs, const ui* eids, ui edge_count) {
                for (ui j = 0; j < edge_count; ++j) {
                    VertexID v_child = nbs[j];
                    if (!candidate_sets[child].count(v_child)) {
                        continue;
                    }
                    std::unordered_map<VertexID, float>::const_iterator it_ub = ub_table[child].find(v_child);
                    if (it_ub == ub_table[child].end()) {
                        continue;
                    }
                    if (!relationMatch(pe.relation_id, kg_.getEdgeRelationId(eids[j]))) {
                        continue;
                    }
                    std::unordered_map<VertexID, float>::iterator it = support_best.find(v_child);
                    if (it == support_best.end() || it_ub->second > it->second) {
                        support_best[v_child] = it_ub->second;
                    }
                }
            };

            if (pe.query_edge_reversed) {
                ui in_count = 0;
                const VertexID* in_nbs = kg_.getInNeighbors(v_parent, in_count);
                const ui* in_eids = kg_.getInEdgeIds(v_parent, in_count);
                collect_one_direction(in_nbs, in_eids, in_count);
                if (undirected_mode_) {
                    ui out_count = 0;
                    const VertexID* out_nbs = kg_.getOutNeighbors(v_parent, out_count);
                    const ui* out_eids = kg_.getOutEdgeIds(v_parent, out_count);
                    collect_one_direction(out_nbs, out_eids, out_count);
                }
            } else {
                ui out_count = 0;
                const VertexID* out_nbs = kg_.getOutNeighbors(v_parent, out_count);
                const ui* out_eids = kg_.getOutEdgeIds(v_parent, out_count);
                collect_one_direction(out_nbs, out_eids, out_count);
                if (undirected_mode_) {
                    ui in_count = 0;
                    const VertexID* in_nbs = kg_.getInNeighbors(v_parent, in_count);
                    const ui* in_eids = kg_.getInEdgeIds(v_parent, in_count);
                    collect_one_direction(in_nbs, in_eids, in_count);
                }
            }
        }
    };

    
    
    
    
    
    
    bool global_changed = true;
    int lb_rounds = 0;
    while (global_changed) {
        global_changed = false;
    for (VertexID child = 0; child < query.num_vertices; ++child) {
        if (parents[child].empty() || candidates[child].empty()) {
            continue;
        }

        std::unordered_set<VertexID> keep;
        const ui full_query_degree = static_cast<ui>(query.out_neighbors[child].size() + query.in_neighbors[child].size());
        if (full_query_degree > parents[child].size()) {
            bool first_neighbor = true;
            for (size_t ei = 0; ei < query.out_neighbors[child].size(); ++ei) {
                const QueryNeighborInfo& qn = query.out_neighbors[child][ei];
                if (qn.neighbor_id >= candidates.size() || candidates[qn.neighbor_id].empty()) {
                    continue;
                }
                std::unordered_set<VertexID> local;
                local.reserve(candidate_sets[child].size() * 2 + 16);
                for (size_t ni = 0; ni < candidates[qn.neighbor_id].size(); ++ni) {
                    VertexID v_neighbor = candidates[qn.neighbor_id][ni];
                    auto collect = [&](const VertexID* nbs, const ui* eids, ui edge_count) {
                        for (ui j = 0; j < edge_count; ++j) {
                            VertexID v_child = nbs[j];
                            if (!candidate_sets[child].count(v_child)) {
                                continue;
                            }
                            if (ub_table[child].find(v_child) == ub_table[child].end()) {
                                continue;
                            }
                            if (!relationMatch(qn.relation_id, kg_.getEdgeRelationId(eids[j]))) {
                                continue;
                            }
                            local.insert(v_child);
                        }
                    };
                    ui in_count = 0;
                    const VertexID* in_nbs = kg_.getInNeighbors(v_neighbor, in_count);
                    const ui* in_eids = kg_.getInEdgeIds(v_neighbor, in_count);
                    collect(in_nbs, in_eids, in_count);
                    if (undirected_mode_) {
                        ui out_count = 0;
                        const VertexID* out_nbs = kg_.getOutNeighbors(v_neighbor, out_count);
                        const ui* out_eids = kg_.getOutEdgeIds(v_neighbor, out_count);
                        collect(out_nbs, out_eids, out_count);
                    }
                }
                if (first_neighbor) {
                    keep.swap(local);
                    first_neighbor = false;
                } else {
                    std::unordered_set<VertexID> inter;
                    inter.reserve(std::min(keep.size(), local.size()) * 2 + 16);
                    if (local.size() < keep.size()) {
                        for (std::unordered_set<VertexID>::const_iterator it = local.begin(); it != local.end(); ++it) {
                            if (keep.count(*it)) {
                                inter.insert(*it);
                            }
                        }
                    } else {
                        for (std::unordered_set<VertexID>::const_iterator it = keep.begin(); it != keep.end(); ++it) {
                            if (local.count(*it)) {
                                inter.insert(*it);
                            }
                        }
                    }
                    keep.swap(inter);
                }
                if (keep.empty()) {
                    break;
                }
            }
            for (size_t ei = 0; ei < query.in_neighbors[child].size(); ++ei) {
                const QueryNeighborInfo& qn = query.in_neighbors[child][ei];
                if (qn.neighbor_id >= candidates.size() || candidates[qn.neighbor_id].empty()) {
                    continue;
                }
                std::unordered_set<VertexID> local;
                local.reserve(candidate_sets[child].size() * 2 + 16);
                for (size_t ni = 0; ni < candidates[qn.neighbor_id].size(); ++ni) {
                    VertexID v_neighbor = candidates[qn.neighbor_id][ni];
                    auto collect = [&](const VertexID* nbs, const ui* eids, ui edge_count) {
                        for (ui j = 0; j < edge_count; ++j) {
                            VertexID v_child = nbs[j];
                            if (!candidate_sets[child].count(v_child)) {
                                continue;
                            }
                            if (ub_table[child].find(v_child) == ub_table[child].end()) {
                                continue;
                            }
                            if (!relationMatch(qn.relation_id, kg_.getEdgeRelationId(eids[j]))) {
                                continue;
                            }
                            local.insert(v_child);
                        }
                    };
                    ui out_count = 0;
                    const VertexID* out_nbs = kg_.getOutNeighbors(v_neighbor, out_count);
                    const ui* out_eids = kg_.getOutEdgeIds(v_neighbor, out_count);
                    collect(out_nbs, out_eids, out_count);
                    if (undirected_mode_) {
                        ui in_count = 0;
                        const VertexID* in_nbs = kg_.getInNeighbors(v_neighbor, in_count);
                        const ui* in_eids = kg_.getInEdgeIds(v_neighbor, in_count);
                        collect(in_nbs, in_eids, in_count);
                    }
                }
                if (first_neighbor) {
                    keep.swap(local);
                    first_neighbor = false;
                } else {
                    std::unordered_set<VertexID> inter;
                    inter.reserve(std::min(keep.size(), local.size()) * 2 + 16);
                    if (local.size() < keep.size()) {
                        for (std::unordered_set<VertexID>::const_iterator it = local.begin(); it != local.end(); ++it) {
                            if (keep.count(*it)) {
                                inter.insert(*it);
                            }
                        }
                    } else {
                        for (std::unordered_set<VertexID>::const_iterator it = keep.begin(); it != keep.end(); ++it) {
                            if (local.count(*it)) {
                                inter.insert(*it);
                            }
                        }
                    }
                    keep.swap(inter);
                }
                if (keep.empty()) {
                    break;
                }
            }
            if (first_neighbor) {
                continue;
            }
        } else if (parents[child].size() == 1) {
            std::unordered_map<VertexID, float> support_best;
            collect_support(parents[child][0], child, support_best);
            if (support_best.empty()) {
                continue;
            }

            const ui child_deg = static_cast<ui>(query.out_neighbors[child].size() + query.in_neighbors[child].size());
            if (child_deg != 1) {
                
                
                continue;
            }
            
            
            
            
            
            
            
            
            
            keep.reserve(support_best.size() * 2 + 16);
            for (std::unordered_map<VertexID, float>::const_iterator it = support_best.begin();
                 it != support_best.end(); ++it) {
                keep.insert(it->first);
            }
        } else {
            bool first_parent = true;
            bool saw_empty_support = false;
            for (size_t pi = 0; pi < parents[child].size(); ++pi) {
                std::unordered_map<VertexID, float> support_best;
                collect_support(parents[child][pi], child, support_best);
                if (support_best.empty()) {
                    keep.clear();
                    saw_empty_support = true;
                    first_parent = false;
                    break;
                }
                if (first_parent) {
                    keep.reserve(support_best.size() * 2 + 16);
                    for (std::unordered_map<VertexID, float>::const_iterator it = support_best.begin();
                         it != support_best.end(); ++it) {
                        keep.insert(it->first);
                    }
                    first_parent = false;
                } else {
                    std::unordered_set<VertexID> inter;
                    inter.reserve(std::min(keep.size(), support_best.size()) * 2 + 16);
                    if (support_best.size() < keep.size()) {
                        for (std::unordered_map<VertexID, float>::const_iterator it = support_best.begin();
                             it != support_best.end(); ++it) {
                            if (keep.count(it->first)) {
                                inter.insert(it->first);
                            }
                        }
                    } else {
                        for (std::unordered_set<VertexID>::const_iterator it = keep.begin();
                             it != keep.end(); ++it) {
                            if (support_best.count(*it)) {
                                inter.insert(*it);
                            }
                        }
                    }
                    keep.swap(inter);
                }
                if (keep.empty()) {
                    break;
                }
            }
            if (saw_empty_support) {
                keep.clear();
            }
        }

        if (keep.size() >= candidates[child].size()) {
            continue;
        }

        std::vector<VertexID> next;
        std::unordered_map<VertexID, float> next_ub;
        next.reserve(keep.size());
        next_ub.reserve(keep.size() * 2 + 16);
        for (size_t i = 0; i < candidates[child].size(); ++i) {
            VertexID v = candidates[child][i];
            if (keep.count(v)) {
                std::unordered_map<VertexID, float>::const_iterator it_ub = ub_table[child].find(v);
                if (it_ub != ub_table[child].end()) {
                    next.push_back(v);
                    next_ub[v] = it_ub->second;
                }
            }
        }
        if (verbose_) {
            std::cerr << "[ub-pack] child=" << child
                      << " parents=" << parents[child].size()
                      << " cand_before=" << candidates[child].size()
                      << " cand_after=" << next.size() << std::endl;
        }
        candidates[child].swap(next);
        ub_table[child].swap(next_ub);
        candidate_sets[child].clear();
        candidate_sets[child].reserve(candidates[child].size() * 2 + 16);
        for (size_t i = 0; i < candidates[child].size(); ++i) {
            candidate_sets[child].insert(candidates[child][i]);
        }
        global_changed = true;
    }

    
    
    if (lb_enabled_ && !global_changed && lb_rounds < 16) {
        ++lb_rounds;

        auto node_sim = [&](VertexID u, VertexID v) -> float {
            const float* emb = (u < query_vertex_embeddings.size()) ? query_vertex_embeddings[u] : NULL;
            return nodeSimilarity(u, emb, v);
        };
        auto kg_has_edge = [&](VertexID s, VertexID d, ui rel) -> bool {
            ui c = 0;
            const VertexID* nb = kg_.getOutNeighbors(s, c);
            const ui* ei = kg_.getOutEdgeIds(s, c);
            for (ui i = 0; i < c; ++i) {
                if (nb[i] == d && relationMatch(rel, kg_.getEdgeRelationId(ei[i]))) {
                    return true;
                }
            }
            return false;
        };
        auto pair_feasible = [&](VertexID v_u, VertexID v_x, ui rel, bool out_from_u) -> bool {
            if (out_from_u) {
                if (kg_has_edge(v_u, v_x, rel)) return true;
                if (undirected_mode_ && kg_has_edge(v_x, v_u, rel)) return true;
            } else {
                if (kg_has_edge(v_x, v_u, rel)) return true;
                if (undirected_mode_ && kg_has_edge(v_u, v_x, rel)) return true;
            }
            return false;
        };

        
        bool any_empty = false;
        std::vector<float> node_max_real(query.num_vertices, -std::numeric_limits<float>::max());
        double total_real = 0.0;
        for (ui u = 0; u < query.num_vertices; ++u) {
            if (candidates[u].empty()) {
                any_empty = true;
                break;
            }
            for (size_t i = 0; i < candidates[u].size(); ++i) {
                float s = node_sim(u, candidates[u][i]);
                if (s > node_max_real[u]) {
                    node_max_real[u] = s;
                }
            }
            total_real += node_max_real[u];
        }

        if (!any_empty) {
            struct Inc { VertexID nb; ui rel; bool out_from_u; };
            std::vector<std::vector<Inc> > incident(query.num_vertices);
            for (ui u = 0; u < query.num_vertices; ++u) {
                for (size_t i = 0; i < query.out_neighbors[u].size(); ++i) {
                    incident[u].push_back(Inc{query.out_neighbors[u][i].neighbor_id,
                                              query.out_neighbors[u][i].relation_id, true});
                }
                for (size_t i = 0; i < query.in_neighbors[u].size(); ++i) {
                    incident[u].push_back(Inc{query.in_neighbors[u][i].neighbor_id,
                                              query.in_neighbors[u][i].relation_id, false});
                }
            }

            std::vector<std::vector<VertexID> > order_by_sim(query.num_vertices);
            for (ui u = 0; u < query.num_vertices; ++u) {
                order_by_sim[u] = candidates[u];
                std::sort(order_by_sim[u].begin(), order_by_sim[u].end(),
                          [&](VertexID a, VertexID b) {
                              float sa = node_sim(u, a);
                              float sb = node_sim(u, b);
                              if (sa != sb) return sa > sb;
                              return a < b;
                          });
            }

            
            std::vector<VertexID> cur(query.num_vertices, std::numeric_limits<VertexID>::max());
            std::priority_queue<float, std::vector<float>, std::greater<float> > kheap;
            long budget = 200000;
            std::function<void(size_t, float)> dfs = [&](size_t idx, float sc) {
                if (budget <= 0) return;
                if (idx >= bfs_order.size()) {
                    kheap.push(sc);
                    if (kheap.size() > top_k) kheap.pop();
                    return;
                }
                VertexID u = bfs_order[idx];
                const std::vector<VertexID>& cand = order_by_sim[u];
                for (size_t ci = 0; ci < cand.size(); ++ci) {
                    if (budget <= 0) break;
                    --budget;
                    VertexID v = cand[ci];
                    bool used = false;
                    for (ui w = 0; w < query.num_vertices; ++w) {
                        if (cur[w] == v) { used = true; break; }
                    }
                    if (used) continue;
                    bool ok = true;
                    for (size_t e = 0; e < incident[u].size(); ++e) {
                        VertexID nb = incident[u][e].nb;
                        if (nb < cur.size() && cur[nb] != std::numeric_limits<VertexID>::max()) {
                            if (!pair_feasible(v, cur[nb], incident[u][e].rel, incident[u][e].out_from_u)) {
                                ok = false;
                                break;
                            }
                        }
                    }
                    if (!ok) continue;
                    cur[u] = v;
                    dfs(idx + 1, sc + node_sim(u, v));
                    cur[u] = std::numeric_limits<VertexID>::max();
                }
            };
            dfs(0, 0.0f);

            if (kheap.size() >= top_k) {
                float s_lb = kheap.top();  
                if (verbose_) {
                    std::cerr << "[ub-lb] S_lb=" << s_lb << " round=" << lb_rounds << std::endl;
                }
                for (ui u = 0; u < query.num_vertices; ++u) {
                    if (candidates[u].empty()) continue;
                    double others = total_real - static_cast<double>(node_max_real[u]);
                    std::vector<VertexID> next;
                    std::unordered_map<VertexID, float> next_ub;
                    next.reserve(candidates[u].size());
                    next_ub.reserve(candidates[u].size() * 2 + 16);
                    for (size_t i = 0; i < candidates[u].size(); ++i) {
                        VertexID v = candidates[u][i];
                        double opt = static_cast<double>(node_sim(u, v)) + others;
                        if (opt >= static_cast<double>(s_lb)) {
                            std::unordered_map<VertexID, float>::const_iterator it_ub = ub_table[u].find(v);
                            if (it_ub != ub_table[u].end()) {
                                next.push_back(v);
                                next_ub[v] = it_ub->second;
                            }
                        }
                    }
                    if (next.size() < candidates[u].size()) {
                        if (verbose_) {
                            std::cerr << "[ub-lb] node=" << u
                                      << " cand_before=" << candidates[u].size()
                                      << " cand_after=" << next.size() << std::endl;
                        }
                        candidates[u].swap(next);
                        ub_table[u].swap(next_ub);
                        candidate_sets[u].clear();
                        candidate_sets[u].reserve(candidates[u].size() * 2 + 16);
                        for (size_t i = 0; i < candidates[u].size(); ++i) {
                            candidate_sets[u].insert(candidates[u][i]);
                        }
                        global_changed = true;
                    }
                }
            }
        }
    }
    }
}
