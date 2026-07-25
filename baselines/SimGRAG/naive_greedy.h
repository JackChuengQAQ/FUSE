#ifndef SUBGRAPHMATCHING_BASELINE_NAIVE_GREEDY_H
#define SUBGRAPHMATCHING_BASELINE_NAIVE_GREEDY_H

#include <chrono>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <limits>

#include "graph/new_graph.h"
#include "configuration/types.h"
#include "newvamana.h"
#include "query_types.h"

class NaiveGreedyMatcher {
public:
    struct MatchResult {
        float score;
        std::vector<std::tuple<VertexID, ui, VertexID> > matched_edges;
    };

    enum class Mode {
        Naive = 0,
        Greedy = 1
    };

public:
    explicit NaiveGreedyMatcher(const EmbeddingDiGraph& kg, bool verbose = false);

    void setUndirectedMode(bool enabled);
    void setWildcardRelationId(ui relation_id);
    void setRelationNameMap(const std::vector<ui>& rel_q2k);
    void setQueryTimeLimitSec(ui seconds);

    bool timedOut() const { return timed_out_; }

    std::vector<MatchResult> retrieve(const QueryPattern& query,
                                      const std::vector<const float*>& query_node_embeddings,
                                      ui top_k,
                                      ui node_sim_topk,
                                      const VamanaANN* node_vamana_ann,
                                      const std::vector<QueryDirectedEdge>& query_edges,
                                      Mode mode = Mode::Greedy);

private:
    struct CandidateEdge {
        ui kg_relation;
        VertexID kg_next;
        float score;
    };

    struct HeapEntry {
        MatchResult result;
        std::vector<VertexID> mapping_key;
    };

    struct HeapCmp {
        bool operator()(const HeapEntry& a, const HeapEntry& b) const {
            return a.result.score < b.result.score;
        }
    };

private:
    float nodeDistance(const float* lhs, const float* rhs, ui dim) const;
    void findSimilarNodes(const QueryPattern& query,
                          const std::vector<const float*>& query_node_embeddings,
                          ui node_sim_topk,
                          const VamanaANN* node_vamana_ann);
    void findSimilarNodesVamana(const QueryPattern& query,
                                const std::vector<const float*>& query_node_embeddings,
                                ui node_sim_topk,
                                const VamanaANN& ann);
    void buildEdgeSequence(const QueryPattern& query);
    void dfsAllEdges(VertexID node, const QueryPattern& query);
    std::vector<float> calculateFinalBestScores(const QueryPattern& query) const;
    void matchRecursive(ui edge_idx,
                        const QueryPattern& query,
                        const std::vector<const float*>& query_node_embeddings,
                        std::unordered_map<VertexID, VertexID>& node_matching,
                        float cur_score,
                        Mode mode,
                        const std::vector<float>& final_best_scores);
    void collectOutgoingCandidates(VertexID kg_src,
                                   ui pattern_rel,
                                   std::vector<CandidateEdge>& out) const;
    bool inSimilarNodeSet(VertexID query_u, VertexID kg_v, float& dist) const;
    bool isKnownQueryNode(VertexID query_u) const;
    bool relationMatch(ui query_rel, ui kg_rel) const;
    bool checkDeadline() const;
    void armDeadline();
    void resetDebugStats(ui num_edges);
    void noteDebugBranch(ui edge_idx,
                         const QueryDirectedEdge& pe,
                         VertexID kg_src,
                         size_t expansions,
                         size_t valid,
                         float cur_score);
    void printDebugSummary(const QueryPattern& query) const;

    bool firstMatchedKgEdge(VertexID qs, VertexID qd, ui query_rel,
                            VertexID& out_src, VertexID& out_dst, ui& out_rel) const;
    bool materializeMatchedEdges(const QueryPattern& query,
                                 const std::unordered_map<VertexID, VertexID>& node_matching,
                                 std::vector<std::tuple<VertexID, ui, VertexID> >& out_edges) const;
    float computeScoreFromMapping(const QueryPattern& query,
                                  const std::unordered_map<VertexID, VertexID>& node_matching) const;
    void fillMappingKey(
        const QueryPattern& query,
        const std::unordered_map<VertexID, VertexID>& node_matching,
        std::vector<VertexID>& key) const;
    void tryPushResult(const QueryPattern& query,
                       const std::unordered_map<VertexID, VertexID>& node_matching,
                       float cur_score);
    const std::vector<QueryDirectedEdge>& outputEdgeList() const;

private:
    const EmbeddingDiGraph& kg_;
    bool verbose_;
    bool undirected_mode_;
    bool timed_out_;
    bool deadline_active_;
    ui top_k_;
    ui node_sim_topk_;
    ui query_time_limit_sec_;
    ui wildcard_relation_id_;
    std::chrono::steady_clock::time_point deadline_;
    std::vector<char> query_node_known_;
    std::vector<ui> rel_q2k_;

    struct DebugDepthStats {
        unsigned long long calls = 0;
        unsigned long long expansion_sum = 0;
        unsigned long long valid_sum = 0;
        unsigned long long branch_sum = 0;
        unsigned long long zero_valid = 0;
        unsigned long long prune_breaks = 0;
        unsigned long long prune_tail_skipped = 0;
        size_t max_expansions = 0;
        size_t max_valid = 0;
    };

    struct DebugSample {
        ui edge_idx = 0;
        VertexID q_src = 0;
        VertexID q_dst = 0;
        ui q_rel = 0;
        VertexID kg_src = 0;
        size_t expansions = 0;
        size_t valid = 0;
        float cur_score = 0.0f;
        size_t heap_size = 0;
    };

    bool debug_enabled_;
    size_t debug_sample_limit_;
    unsigned long long debug_leaf_calls_;
    unsigned long long debug_result_attempts_;
    unsigned long long debug_result_materialize_fail_;
    unsigned long long debug_result_accepts_;
    unsigned long long debug_deadline_hits_;
    size_t debug_root_candidates_;
    unsigned long long debug_root_iterations_;
    unsigned long long debug_root_prunes_;
    std::vector<DebugDepthStats> debug_depth_stats_;
    std::vector<DebugSample> debug_samples_;

    std::vector<std::vector<std::pair<VertexID, float> > > similar_nodes_;
    std::vector<std::unordered_map<VertexID, float> > similar_nodes_map_;

    std::vector<QueryDirectedEdge> edge_sequence_;
    std::vector<QueryDirectedEdge> query_edges_;
    std::set<std::tuple<VertexID, ui, VertexID> > visited_edges_;
    std::vector<VertexID> mapping_key_scratch_;
    
    
    std::set<std::vector<VertexID> > heap_mapping_signatures_;

    std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapCmp> result_heap_;
};

#endif
