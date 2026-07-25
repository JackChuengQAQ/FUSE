#include "naive_greedy.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stack>
#include <utility>

namespace {
bool envFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    std::string s(value);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s != "0" && s != "false" && s != "off" && s != "no";
}

size_t envSizeOrDefault(const char* name, size_t fallback) {
    const char* value = std::getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    char* end = NULL;
    unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value) {
        return fallback;
    }
    return static_cast<size_t>(parsed);
}

}  

NaiveGreedyMatcher::NaiveGreedyMatcher(const EmbeddingDiGraph& kg, bool verbose)
    : kg_(kg),
      verbose_(verbose),
      undirected_mode_(false),
      timed_out_(false),
      deadline_active_(false),
      top_k_(0),
      node_sim_topk_(0),
      query_time_limit_sec_(3600),
      wildcard_relation_id_(std::numeric_limits<ui>::max()),
      debug_enabled_(envFlagEnabled("NAIVE_GREEDY_DEBUG")),
      debug_sample_limit_(envSizeOrDefault("NAIVE_GREEDY_DEBUG_SAMPLES", 16)),
      debug_leaf_calls_(0),
      debug_result_attempts_(0),
      debug_result_materialize_fail_(0),
      debug_result_accepts_(0),
      debug_deadline_hits_(0),
      debug_root_candidates_(0),
      debug_root_iterations_(0),
      debug_root_prunes_(0) {
}

void NaiveGreedyMatcher::setUndirectedMode(bool enabled) {
    undirected_mode_ = enabled;
}

void NaiveGreedyMatcher::setWildcardRelationId(ui relation_id) {
    wildcard_relation_id_ = relation_id;
}

void NaiveGreedyMatcher::setRelationNameMap(const std::vector<ui>& rel_q2k) {
    rel_q2k_ = rel_q2k;
}

void NaiveGreedyMatcher::setQueryTimeLimitSec(ui seconds) {
    query_time_limit_sec_ = seconds;
}

void NaiveGreedyMatcher::armDeadline() {
    timed_out_ = false;
    if (query_time_limit_sec_ == 0) {
        deadline_active_ = false;
        return;
    }
    deadline_active_ = true;
    deadline_ = std::chrono::steady_clock::now() +
                std::chrono::seconds(static_cast<long long>(query_time_limit_sec_));
}

bool NaiveGreedyMatcher::checkDeadline() const {
    if (!deadline_active_) {
        return false;
    }
    return std::chrono::steady_clock::now() >= deadline_;
}

void NaiveGreedyMatcher::resetDebugStats(ui num_edges) {
    debug_leaf_calls_ = 0;
    debug_result_attempts_ = 0;
    debug_result_materialize_fail_ = 0;
    debug_result_accepts_ = 0;
    debug_deadline_hits_ = 0;
    debug_root_candidates_ = 0;
    debug_root_iterations_ = 0;
    debug_root_prunes_ = 0;
    debug_depth_stats_.assign(num_edges, DebugDepthStats());
    debug_samples_.clear();
}

void NaiveGreedyMatcher::noteDebugBranch(ui edge_idx,
                                         const QueryDirectedEdge& pe,
                                         VertexID kg_src,
                                         size_t expansions,
                                         size_t valid,
                                         float cur_score) {
    if (!debug_enabled_ || edge_idx >= debug_depth_stats_.size()) {
        return;
    }
    DebugDepthStats& st = debug_depth_stats_[edge_idx];
    st.expansion_sum += static_cast<unsigned long long>(expansions);
    st.valid_sum += static_cast<unsigned long long>(valid);
    if (valid == 0) {
        ++st.zero_valid;
    }
    if (expansions > st.max_expansions) {
        st.max_expansions = expansions;
    }
    if (valid > st.max_valid) {
        st.max_valid = valid;
    }

    DebugSample sample;
    sample.edge_idx = edge_idx;
    sample.q_src = pe.src;
    sample.q_dst = pe.dst;
    sample.q_rel = pe.relation_id;
    sample.kg_src = kg_src;
    sample.expansions = expansions;
    sample.valid = valid;
    sample.cur_score = cur_score;
    sample.heap_size = result_heap_.size();

    auto key = [](const DebugSample& s) -> unsigned long long {
        return static_cast<unsigned long long>(s.expansions) +
               static_cast<unsigned long long>(s.valid);
    };

    if (debug_samples_.size() < debug_sample_limit_) {
        debug_samples_.push_back(sample);
        return;
    }
    if (debug_samples_.empty()) {
        return;
    }
    size_t min_pos = 0;
    unsigned long long min_key = key(debug_samples_[0]);
    for (size_t i = 1; i < debug_samples_.size(); ++i) {
        unsigned long long cur_key = key(debug_samples_[i]);
        if (cur_key < min_key) {
            min_key = cur_key;
            min_pos = i;
        }
    }
    if (key(sample) > min_key) {
        debug_samples_[min_pos] = sample;
    }
}

void NaiveGreedyMatcher::printDebugSummary(const QueryPattern& query) const {
    if (!debug_enabled_) {
        return;
    }
    std::cerr << "[naive-debug] summary"
              << " top_k=" << top_k_
              << " node_sim_topk=" << node_sim_topk_
              << " timed_out=" << (timed_out_ ? 1 : 0)
              << " root_candidates=" << debug_root_candidates_
              << " root_iterations=" << debug_root_iterations_
              << " root_prunes=" << debug_root_prunes_
              << " leaf_calls=" << debug_leaf_calls_
              << " result_attempts=" << debug_result_attempts_
              << " materialize_fail=" << debug_result_materialize_fail_
              << " result_accepts=" << debug_result_accepts_
              << " heap_size=" << result_heap_.size()
              << " deadline_hits=" << debug_deadline_hits_
              << std::endl;

    std::cerr << "[naive-debug] similar_nodes";
    for (ui u = 0; u < query.num_vertices; ++u) {
        size_t sz = (u < similar_nodes_.size()) ? similar_nodes_[u].size() : 0;
        std::cerr << " q" << u << "=" << sz;
    }
    std::cerr << std::endl;

    for (size_t i = 0; i < edge_sequence_.size(); ++i) {
        const QueryDirectedEdge& e = edge_sequence_[i];
        const DebugDepthStats& st = debug_depth_stats_[i];
        double avg_exp = st.calls == 0 ? 0.0 :
            static_cast<double>(st.expansion_sum) / static_cast<double>(st.calls);
        double avg_valid = st.calls == 0 ? 0.0 :
            static_cast<double>(st.valid_sum) / static_cast<double>(st.calls);
        std::cerr << "[naive-debug] depth=" << i
                  << " qedge=" << e.src << "->" << e.dst
                  << " rel=" << e.relation_id
                  << " calls=" << st.calls
                  << " expansion_sum=" << st.expansion_sum
                  << " valid_sum=" << st.valid_sum
                  << " avg_exp=" << avg_exp
                  << " avg_valid=" << avg_valid
                  << " max_exp=" << st.max_expansions
                  << " max_valid=" << st.max_valid
                  << " zero_valid=" << st.zero_valid
                  << " branches=" << st.branch_sum
                  << " prune_breaks=" << st.prune_breaks
                  << " prune_tail_skipped=" << st.prune_tail_skipped
                  << std::endl;
    }

    std::vector<DebugSample> samples = debug_samples_;
    std::sort(samples.begin(), samples.end(), [](const DebugSample& a, const DebugSample& b) {
        unsigned long long ka = static_cast<unsigned long long>(a.expansions) +
                                static_cast<unsigned long long>(a.valid);
        unsigned long long kb = static_cast<unsigned long long>(b.expansions) +
                                static_cast<unsigned long long>(b.valid);
        if (ka != kb) return ka > kb;
        if (a.valid != b.valid) return a.valid > b.valid;
        return a.edge_idx < b.edge_idx;
    });
    for (size_t i = 0; i < samples.size(); ++i) {
        const DebugSample& s = samples[i];
        std::cerr << "[naive-debug] sample rank=" << (i + 1)
                  << " depth=" << s.edge_idx
                  << " qedge=" << s.q_src << "->" << s.q_dst
                  << " rel=" << s.q_rel
                  << " kg_src=" << s.kg_src
                  << " expansions=" << s.expansions
                  << " valid=" << s.valid
                  << " cur_score=" << s.cur_score
                  << " heap_size=" << s.heap_size
                  << std::endl;
    }
}

float NaiveGreedyMatcher::nodeDistance(const float* lhs, const float* rhs, ui dim) const {
    float dist2 = 0.0f;
    for (ui i = 0; i < dim; ++i) {
        float diff = lhs[i] - rhs[i];
        dist2 += diff * diff;
    }
    return std::sqrt(dist2);
}

void NaiveGreedyMatcher::findSimilarNodesVamana(const QueryPattern& query,
                                                const std::vector<const float*>& query_node_embeddings,
                                                ui node_sim_topk,
                                                const VamanaANN& ann) {
    for (ui u = 0; u < query.num_vertices; ++u) {
        if (u >= query_node_embeddings.size() || query_node_embeddings[u] == NULL) {
            continue;
        }
        VamanaANN::SearchConfig search_cfg;
        search_cfg.use_graph_search = true;
        search_cfg.ef_search = VamanaANN::computeNaiveLsearch(node_sim_topk);
        std::vector<VamanaANN::SearchResult> res =
            ann.search(query_node_embeddings[u], node_sim_topk, search_cfg);
        similar_nodes_[u].reserve(res.size());
        similar_nodes_map_[u].reserve(res.size() * 2 + 16);
        for (size_t i = 0; i < res.size(); ++i) {
            VertexID v = res[i].id;
            float dist = std::sqrt(std::max(0.0f, res[i].distance));
            similar_nodes_[u].push_back(std::make_pair(v, dist));
            similar_nodes_map_[u][v] = dist;
        }
    }
}

void NaiveGreedyMatcher::findSimilarNodes(const QueryPattern& query,
                                          const std::vector<const float*>& query_node_embeddings,
                                          ui node_sim_topk,
                                          const VamanaANN* node_vamana_ann) {
    similar_nodes_.assign(query.num_vertices, std::vector<std::pair<VertexID, float> >());
    similar_nodes_map_.assign(query.num_vertices, std::unordered_map<VertexID, float>());

    if (node_sim_topk == 0) {
        return;
    }

    if (node_vamana_ann == NULL) {
        return;
    }
    findSimilarNodesVamana(query, query_node_embeddings, node_sim_topk, *node_vamana_ann);
}

void NaiveGreedyMatcher::dfsAllEdges(VertexID node, const QueryPattern& query) {
    for (size_t i = 0; i < query.out_neighbors[node].size(); ++i) {
        VertexID nb = query.out_neighbors[node][i].neighbor_id;
        ui rel = query.out_neighbors[node][i].relation_id;
        std::tuple<VertexID, ui, VertexID> key(std::min(node, nb), rel, std::max(node, nb));
        if (visited_edges_.insert(key).second) {
            edge_sequence_.push_back(QueryDirectedEdge{node, nb, rel});
            dfsAllEdges(nb, query);
        }
    }
    for (size_t i = 0; i < query.in_neighbors[node].size(); ++i) {
        VertexID nb = query.in_neighbors[node][i].neighbor_id;
        ui rel = query.in_neighbors[node][i].relation_id;
        std::tuple<VertexID, ui, VertexID> key(std::min(node, nb), rel, std::max(node, nb));
        if (visited_edges_.insert(key).second) {
            
            edge_sequence_.push_back(QueryDirectedEdge{node, nb, rel});
            dfsAllEdges(nb, query);
        }
    }
}

void NaiveGreedyMatcher::buildEdgeSequence(const QueryPattern& query) {
    edge_sequence_.clear();
    visited_edges_.clear();

    VertexID root = 0;
    ui best_deg = 0;
    bool found_known = false;
    for (ui u = 0; u < query.num_vertices; ++u) {
        if (!isKnownQueryNode(u)) {
            continue;
        }
        ui deg = static_cast<ui>(query.out_neighbors[u].size() + query.in_neighbors[u].size());
        if (!found_known || deg > best_deg) {
            best_deg = deg;
            root = u;
            found_known = true;
        }
    }
    if (!found_known) {
        for (ui u = 0; u < query.num_vertices; ++u) {
            ui deg = static_cast<ui>(query.out_neighbors[u].size() + query.in_neighbors[u].size());
            if (deg > best_deg) {
                best_deg = deg;
                root = u;
            }
        }
    }
    dfsAllEdges(root, query);
}

bool NaiveGreedyMatcher::inSimilarNodeSet(VertexID query_u, VertexID kg_v, float& dist) const {
    if (query_u >= similar_nodes_map_.size()) return false;
    std::unordered_map<VertexID, float>::const_iterator it = similar_nodes_map_[query_u].find(kg_v);
    if (it == similar_nodes_map_[query_u].end()) return false;
    dist = it->second;
    return true;
}

bool NaiveGreedyMatcher::isKnownQueryNode(VertexID query_u) const {
    return query_u < query_node_known_.size() && query_node_known_[query_u] != 0;
}

bool NaiveGreedyMatcher::relationMatch(ui query_rel, ui kg_rel) const {
    if (query_rel == wildcard_relation_id_) {
        return true;
    }
    if (query_rel >= rel_q2k_.size()) {
        return false;
    }
    ui mapped = rel_q2k_[query_rel];
    if (mapped == std::numeric_limits<ui>::max()) {
        return false;
    }
    return mapped == kg_rel;
}

void NaiveGreedyMatcher::collectOutgoingCandidates(VertexID kg_src,
                                                   ui pattern_rel,
                                                   std::vector<CandidateEdge>& out) const {
    out.clear();
    ui out_count = 0;
    const VertexID* out_nbs = kg_.getOutNeighbors(kg_src, out_count);
    const ui* out_eids = kg_.getOutEdgeIds(kg_src, out_count);
    for (ui i = 0; i < out_count; ++i) {
        ui kg_rel = kg_.getEdgeRelationId(out_eids[i]);
        if (!relationMatch(pattern_rel, kg_rel)) {
            continue;
        }
        out.push_back(CandidateEdge{kg_rel, out_nbs[i], 0.0f});
    }
    if (undirected_mode_) {
        ui in_count = 0;
        const VertexID* in_nbs = kg_.getInNeighbors(kg_src, in_count);
        const ui* in_eids = kg_.getInEdgeIds(kg_src, in_count);
        for (ui i = 0; i < in_count; ++i) {
            ui kg_rel = kg_.getEdgeRelationId(in_eids[i]);
            if (!relationMatch(pattern_rel, kg_rel)) {
                continue;
            }
            out.push_back(CandidateEdge{kg_rel, in_nbs[i], 0.0f});
        }
    }

    if (out.size() > 1) {
        std::sort(out.begin(), out.end(), [](const CandidateEdge& a, const CandidateEdge& b) {
            if (a.kg_next != b.kg_next) return a.kg_next < b.kg_next;
            return a.kg_relation < b.kg_relation;
        });
        out.erase(std::unique(out.begin(), out.end(),
                              [](const CandidateEdge& a, const CandidateEdge& b) {
                                  return a.kg_next == b.kg_next && a.kg_relation == b.kg_relation;
                              }),
                  out.end());
    }
}

std::vector<float> NaiveGreedyMatcher::calculateFinalBestScores(const QueryPattern& query) const {
    std::vector<float> scores(edge_sequence_.size(), 0.0f);
    if (edge_sequence_.empty()) return scores;

    for (int i = static_cast<int>(edge_sequence_.size()) - 1; i > 0; --i) {
        scores[i - 1] = scores[i];
        const QueryDirectedEdge& e = edge_sequence_[i];

        if (isKnownQueryNode(e.dst) &&
            e.dst < similar_nodes_.size() &&
            !similar_nodes_[e.dst].empty()) {
            scores[i - 1] += similar_nodes_[e.dst][0].second;
        }
    }
    (void)query;
    return scores;
}

const std::vector<QueryDirectedEdge>& NaiveGreedyMatcher::outputEdgeList() const {
    if (!query_edges_.empty()) {
        return query_edges_;
    }
    return edge_sequence_;
}

bool NaiveGreedyMatcher::firstMatchedKgEdge(VertexID qs, VertexID qd, ui query_rel,
                                            VertexID& out_src, VertexID& out_dst,
                                            ui& out_rel) const {
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
            out_rel = rel_kg;
            return true;
        }
        return false;
    };

    out_src = qs;
    out_dst = qd;
    if (scan(qs, qd)) {
        return true;
    }
    if (undirected_mode_ && scan(qd, qs)) {
        return true;
    }
    return false;
}

bool NaiveGreedyMatcher::materializeMatchedEdges(
    const QueryPattern& query,
    const std::unordered_map<VertexID, VertexID>& node_matching,
    std::vector<std::tuple<VertexID, ui, VertexID> >& out_edges) const {
    out_edges.clear();
    const std::vector<QueryDirectedEdge>& edges = outputEdgeList();
    if (edges.empty()) {
        return false;
    }

    std::set<std::tuple<VertexID, ui, VertexID> > seen_kg_edges;
    for (size_t i = 0; i < edges.size(); ++i) {
        const QueryDirectedEdge& qe = edges[i];
        if (qe.src >= query.num_vertices || qe.dst >= query.num_vertices) {
            return false;
        }
        std::unordered_map<VertexID, VertexID>::const_iterator it_src = node_matching.find(qe.src);
        std::unordered_map<VertexID, VertexID>::const_iterator it_dst = node_matching.find(qe.dst);
        if (it_src == node_matching.end() || it_dst == node_matching.end()) {
            return false;
        }
        VertexID vs = it_src->second;
        VertexID vd = it_dst->second;
        VertexID ksrc = 0;
        VertexID kdst = 0;
        ui krel = 0;
        if (!firstMatchedKgEdge(vs, vd, qe.relation_id, ksrc, kdst, krel)) {
            return false;
        }
        std::tuple<VertexID, ui, VertexID> norm_key(
            std::min(ksrc, kdst), krel, std::max(ksrc, kdst));
        if (!seen_kg_edges.insert(norm_key).second) {
            return false;
        }
        
        VertexID lo = std::min(vs, vd);
        VertexID hi = std::max(vs, vd);
        out_edges.push_back(std::make_tuple(lo, krel, hi));
    }
    return out_edges.size() == edges.size();
}

float NaiveGreedyMatcher::computeScoreFromMapping(
    const QueryPattern& query,
    const std::unordered_map<VertexID, VertexID>& node_matching) const {
    float score = 0.0f;
    for (ui u = 0; u < query.num_vertices; ++u) {
        if (!isKnownQueryNode(u)) {
            continue;
        }
        std::unordered_map<VertexID, VertexID>::const_iterator it = node_matching.find(u);
        if (it == node_matching.end()) {
            continue;
        }
        float dist = 0.0f;
        if (!inSimilarNodeSet(u, it->second, dist)) {
            continue;
        }
        score += dist;
    }
    (void)query;
    return score;
}

void NaiveGreedyMatcher::fillMappingKey(
    const QueryPattern& query,
    const std::unordered_map<VertexID, VertexID>& node_matching,
    std::vector<VertexID>& key) const {
    key.resize(query.num_vertices);
    std::fill(key.begin(), key.end(), std::numeric_limits<VertexID>::max());
    for (ui u = 0; u < query.num_vertices; ++u) {
        std::unordered_map<VertexID, VertexID>::const_iterator it = node_matching.find(u);
        if (it != node_matching.end()) {
            key[u] = it->second;
        }
    }
}

void NaiveGreedyMatcher::tryPushResult(const QueryPattern& query,
                                       const std::unordered_map<VertexID, VertexID>& node_matching,
                                       float cur_score) {
    (void)cur_score;
    if (debug_enabled_) {
        ++debug_result_attempts_;
    }

    fillMappingKey(query, node_matching, mapping_key_scratch_);
    if (heap_mapping_signatures_.find(mapping_key_scratch_) != heap_mapping_signatures_.end()) {
        return;
    }

    MatchResult r;
    r.matched_edges.clear();
    if (!materializeMatchedEdges(query, node_matching, r.matched_edges)) {
        if (debug_enabled_) {
            ++debug_result_materialize_fail_;
        }
        return;
    }
    r.score = computeScoreFromMapping(query, node_matching);

    if (result_heap_.size() >= top_k_ &&
        !(r.score < result_heap_.top().result.score)) {
        return;
    }

    HeapEntry entry;
    entry.result = std::move(r);
    entry.mapping_key = mapping_key_scratch_;

    if (result_heap_.size() >= top_k_) {
        heap_mapping_signatures_.erase(result_heap_.top().mapping_key);
        result_heap_.pop();
    }

    heap_mapping_signatures_.insert(entry.mapping_key);
    result_heap_.push(std::move(entry));
    if (debug_enabled_) {
        ++debug_result_accepts_;
    }
}

void NaiveGreedyMatcher::matchRecursive(ui edge_idx,
                                        const QueryPattern& query,
                                        const std::vector<const float*>& query_node_embeddings,
                                        std::unordered_map<VertexID, VertexID>& node_matching,
                                        float cur_score,
                                        Mode mode,
                                        const std::vector<float>& final_best_scores) {
    (void)query_node_embeddings;
    if (debug_enabled_) {
        if (edge_idx < debug_depth_stats_.size()) {
            ++debug_depth_stats_[edge_idx].calls;
        } else if (edge_idx == edge_sequence_.size()) {
            ++debug_leaf_calls_;
        }
    }
    if (checkDeadline()) {
        timed_out_ = true;
        if (debug_enabled_) {
            ++debug_deadline_hits_;
        }
        return;
    }

    if (edge_idx == edge_sequence_.size()) {
        tryPushResult(query, node_matching, cur_score);
        return;
    }

    const QueryDirectedEdge& pe = edge_sequence_[edge_idx];
    if (!node_matching.count(pe.src)) {
        return;
    }
    VertexID kg_src = node_matching[pe.src];

    std::vector<CandidateEdge> expansions;
    collectOutgoingCandidates(kg_src, pe.relation_id, expansions);

    std::vector<CandidateEdge> valid;
    valid.reserve(expansions.size());
    for (size_t i = 0; i < expansions.size(); ++i) {
        const CandidateEdge& cand = expansions[i];
        float next_score = cur_score + cand.score;
        if (node_matching.count(pe.dst)) {
            if (node_matching[pe.dst] != cand.kg_next) {
                continue;
            }
        } else {
            if (isKnownQueryNode(pe.dst)) {
                float node_dist = 0.0f;
                if (!inSimilarNodeSet(pe.dst, cand.kg_next, node_dist)) {
                    continue;
                }
                next_score += node_dist;
            }
        }
        valid.push_back(CandidateEdge{cand.kg_relation, cand.kg_next, next_score});
    }

    if (mode == Mode::Greedy) {
        std::sort(valid.begin(), valid.end(),
                  [](const CandidateEdge& a, const CandidateEdge& b) { return a.score < b.score; });
    }
    noteDebugBranch(edge_idx, pe, kg_src, expansions.size(), valid.size(), cur_score);

    for (size_t i = 0; i < valid.size(); ++i) {
        if (checkDeadline()) {
            timed_out_ = true;
            if (debug_enabled_) {
                ++debug_deadline_hits_;
            }
            return;
        }
        const CandidateEdge& cand = valid[i];
        if (mode == Mode::Greedy && result_heap_.size() >= top_k_ &&
            edge_idx < final_best_scores.size() &&
            cand.score + final_best_scores[edge_idx] > result_heap_.top().result.score) {
            if (debug_enabled_ && edge_idx < debug_depth_stats_.size()) {
                ++debug_depth_stats_[edge_idx].prune_breaks;
                debug_depth_stats_[edge_idx].prune_tail_skipped +=
                    static_cast<unsigned long long>(valid.size() - i);
            }
            break;
        }

        if (debug_enabled_ && edge_idx < debug_depth_stats_.size()) {
            ++debug_depth_stats_[edge_idx].branch_sum;
        }
        std::unordered_map<VertexID, VertexID> next_matching = node_matching;
        next_matching[pe.dst] = cand.kg_next;
        matchRecursive(edge_idx + 1, query, query_node_embeddings, next_matching, cand.score, mode,
                       final_best_scores);
        if (timed_out_) {
            return;
        }
    }
}

std::vector<NaiveGreedyMatcher::MatchResult> NaiveGreedyMatcher::retrieve(
    const QueryPattern& query,
    const std::vector<const float*>& query_node_embeddings,
    ui top_k,
    ui node_sim_topk,
    const VamanaANN* node_vamana_ann,
    const std::vector<QueryDirectedEdge>& query_edges,
    Mode mode) {
    top_k_ = top_k;
    node_sim_topk_ = node_sim_topk;
    query_edges_ = query_edges;
    armDeadline();

    query_node_known_.assign(query.num_vertices, 0);
    for (ui u = 0; u < query.num_vertices; ++u) {
        if (u < query_node_embeddings.size() && query_node_embeddings[u] != NULL) {
            query_node_known_[u] = 1;
        }
    }

    while (!result_heap_.empty()) {
        result_heap_.pop();
    }
    mapping_key_scratch_.assign(query.num_vertices, std::numeric_limits<VertexID>::max());
    heap_mapping_signatures_.clear();

    if (query.num_vertices == 0 || top_k_ == 0) {
        return std::vector<MatchResult>();
    }

    if (node_vamana_ann == NULL) {
        if (verbose_) {
            std::cerr << "[NaiveGreedyMatcher] Vamana ANN pointer is required." << std::endl;
        }
        return std::vector<MatchResult>();
    }

    findSimilarNodes(query, query_node_embeddings, node_sim_topk_, node_vamana_ann);
    if (checkDeadline()) {
        timed_out_ = true;
        return std::vector<MatchResult>();
    }

    buildEdgeSequence(query);
    if (edge_sequence_.empty()) {
        resetDebugStats(0);
        printDebugSummary(query);
        return std::vector<MatchResult>();
    }
    resetDebugStats(static_cast<ui>(edge_sequence_.size()));

    VertexID root = edge_sequence_[0].src;
    std::vector<std::pair<VertexID, float> > root_candidates;
    if (isKnownQueryNode(root)) {
        root_candidates = similar_nodes_[root];
        std::sort(root_candidates.begin(), root_candidates.end(),
                  [](const std::pair<VertexID, float>& a, const std::pair<VertexID, float>& b) {
                      if (a.second != b.second) return a.second < b.second;
                      return a.first < b.first;
                  });
    } else {
        root_candidates.reserve(kg_.getVerticesCount());
        for (VertexID v = 0; v < kg_.getVerticesCount(); ++v) {
            root_candidates.push_back(std::make_pair(v, 0.0f));
        }
    }
    if (debug_enabled_) {
        debug_root_candidates_ = root_candidates.size();
    }

    std::vector<float> final_best_scores;
    if (mode == Mode::Greedy) {
        final_best_scores = calculateFinalBestScores(query);
    }

    for (size_t i = 0; i < root_candidates.size(); ++i) {
        if (checkDeadline()) {
            timed_out_ = true;
            if (debug_enabled_) {
                ++debug_deadline_hits_;
            }
            break;
        }
        std::unordered_map<VertexID, VertexID> node_matching;
        node_matching[root] = root_candidates[i].first;
        float initial_score = root_candidates[i].second;
        if (mode == Mode::Greedy &&
            result_heap_.size() >= top_k_ &&
            !final_best_scores.empty() &&
            initial_score + final_best_scores[0] > result_heap_.top().result.score) {
            if (debug_enabled_) {
                ++debug_root_prunes_;
            }
            continue;
        }
        if (debug_enabled_) {
            ++debug_root_iterations_;
        }
        matchRecursive(0, query, query_node_embeddings, node_matching, initial_score, mode,
                       final_best_scores);
        if (timed_out_) {
            break;
        }
    }

    printDebugSummary(query);

    std::vector<MatchResult> out(result_heap_.size());
    for (int i = static_cast<int>(out.size()) - 1; i >= 0; --i) {
        out[i] = result_heap_.top().result;
        result_heap_.pop();
    }
    heap_mapping_signatures_.clear();

    if (verbose_) {
        std::cout << "[NaiveGreedyMatcher] mode="
                  << (mode == Mode::Greedy ? "greedy" : "naive")
                  << " timed_out=" << (timed_out_ ? "1" : "0")
                  << " results=" << out.size() << std::endl;
    }
    return out;
}
