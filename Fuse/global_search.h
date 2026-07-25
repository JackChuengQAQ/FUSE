#ifndef SUBGRAPHMATCHING_TOPK_GLOBAL_SEARCH_H
#define SUBGRAPHMATCHING_TOPK_GLOBAL_SEARCH_H

#include <cstdint>
#include <limits>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "configuration/types.h"
#include "new_graph.h"
#include "cover_refinement.h"
#include "upper_bound_pruning.h"




struct SearchRunStats {
    uint64_t states_pushed = 0;
    uint64_t states_popped = 0;
};

class GlobalSearcher {
public:
    struct SearchResult {
        float score;
        std::vector<VertexID> mapping;
        std::vector<std::string> node_names;
        
        
        std::vector<std::tuple<std::string, std::string, std::string, float> > matched_edges;
    };

public:
    explicit GlobalSearcher(const EmbeddingDiGraph& kg, bool verbose = false);

    void setRelationNameMap(const std::vector<ui>& rel_q2k);
    void setUndirectedMode(bool enabled);

    const SearchRunStats& getLastSearchStats() const { return last_search_stats_; }

    std::vector<SearchResult> searchTopK(const QueryPattern& query,
                                         const std::vector<const float*>& query_vertex_embeddings,
                                         const std::vector<VertexID>& bfs_order,
                                         const std::vector<std::vector<UpperBoundPruner::DagEdge> >& dag_children,
                                         const std::vector<std::vector<VertexID> >& candidates,
                                         const std::vector<std::unordered_map<VertexID, float> >& ub_table,
                                         ui top_k,
                                         const std::vector<QueryDirectedEdge>* ordered_query_edges = NULL) const;

private:
    struct PartialState {
        float ub;
        float current_score;
        std::vector<VertexID> mapping;
        ui next_idx;
    };

private:
    float nodeSimilarity(const float* query_emb, VertexID kg_v) const;
    bool relationMatch(ui query_rel, ui kg_rel) const;
    
    
    
    bool firstMatchedKgEdge(VertexID src, VertexID dst, ui query_rel,
                            VertexID& out_src, VertexID& out_dst, ui& out_rel) const;
    bool isUsed(const std::vector<VertexID>& mapping, VertexID kg_v) const;

private:
    const EmbeddingDiGraph& kg_;
    bool verbose_;
    bool undirected_mode_;
    std::vector<ui> rel_q2k_;
    mutable SearchRunStats last_search_stats_;
};

#endif
