#ifndef SUBGRAPHMATCHING_TOPK_UPPER_BOUND_PRUNING_H
#define SUBGRAPHMATCHING_TOPK_UPPER_BOUND_PRUNING_H

#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "configuration/types.h"
#include "new_graph.h"
#include "cover_refinement.h"

class UpperBoundPruner {
public:
    struct DagEdge {
        VertexID child;
        ui relation_id;
        
        bool query_edge_reversed;
    };

public:
    explicit UpperBoundPruner(const EmbeddingDiGraph& kg, bool verbose = false);

    
    void setRelationNameMap(const std::vector<ui>& rel_q2k);
    void setUndirectedMode(bool enabled);
    void setLowerBoundEnabled(bool enabled);

    VertexID selectRootByMaxDegree(const QueryPattern& query) const;
    VertexID selectRootByCandidateSet(const QueryPattern& query,
                                      const std::vector<std::vector<VertexID> >& candidates) const;
    void buildDfsAndDag(const QueryPattern& query,
                        VertexID root,
                        std::vector<VertexID>& bfs_order,
                        std::vector<std::vector<DagEdge> >& dag_children) const;

    void computeUpperBounds(const QueryPattern& query,
                            const std::vector<const float*>& query_vertex_embeddings,
                            const std::vector<VertexID>& bfs_order,
                            const std::vector<std::vector<DagEdge> >& dag_children,
                            const std::vector<std::vector<VertexID> >& candidates,
                            std::vector<std::unordered_map<VertexID, float> >& ub_table) const;

    void pruneByRootTopKAndDescendants(const QueryPattern& query,
                                       const std::vector<VertexID>& bfs_order,
                                       const std::vector<std::vector<DagEdge> >& dag_children,
                                       ui root_top_k,
                                       std::vector<std::vector<VertexID> >& candidates,
                                       std::vector<std::unordered_map<VertexID, float> >& ub_table) const;

    void packSupportedCandidates(const QueryPattern& query,
                                 const std::vector<const float*>& query_vertex_embeddings,
                                 const std::vector<VertexID>& bfs_order,
                                 const std::vector<std::vector<DagEdge> >& dag_children,
                                 ui top_k,
                                 std::vector<std::vector<VertexID> >& candidates,
                                 std::vector<std::unordered_map<VertexID, float> >& ub_table) const;

private:
    float nodeSimilarity(VertexID query_u, const float* query_emb, VertexID kg_v) const;
    bool relationMatch(ui query_rel, ui kg_rel) const;

private:
    const EmbeddingDiGraph& kg_;
    bool verbose_;
    bool undirected_mode_;
    bool lb_enabled_ = false;
    std::vector<ui> rel_q2k_;
};

#endif
