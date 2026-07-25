#ifndef SUBGRAPHMATCHING_TOPK_COVER_REFINEMENT_H
#define SUBGRAPHMATCHING_TOPK_COVER_REFINEMENT_H

#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cstdint>

#include "configuration/types.h"
#include "newvamana.h"
#include "new_graph.h"






struct CandInitStats {
    uint64_t pool_size = 0;        
    uint64_t kept_by_degree = 0;   
    uint64_t sim_computations = 0; 
    uint64_t kept_by_topn = 0;     
    uint64_t wildcard_nodes = 0;   
};

struct QueryNeighborInfo {
    VertexID neighbor_id;
    ui relation_id;
};

struct QueryDirectedEdge {
    VertexID src;
    VertexID dst;
    ui relation_id;
};

struct QueryPattern {
    ui num_vertices;
    std::vector<std::vector<QueryNeighborInfo> > out_neighbors;
    std::vector<std::vector<QueryNeighborInfo> > in_neighbors;
};

class VertexCoverRefiner {
public:
    enum class LogMode {
        Silent = 0,
        Debug = 1
    };

    explicit VertexCoverRefiner(const EmbeddingDiGraph& kg, bool verbose = false);

    static QueryPattern buildQueryPattern(ui num_vertices, const std::vector<QueryDirectedEdge>& edges);
    static std::unordered_set<VertexID> buildIndependentSet(
        ui num_vertices, const std::unordered_set<VertexID>& cover_set);

    std::unordered_set<VertexID> computeMinVertexCover(const QueryPattern& query) const;

    
    
    
    
    
    void initializeCandidatesForCover(const QueryPattern& query,
                                      const std::unordered_set<VertexID>& cover_set,
                                      std::vector<std::vector<VertexID> >& candidates,
                                      ui top_n,
                                      const std::vector<const float*>& query_vertex_embeddings,
                                      ui per_node_cap = 0) const;

    void iterativeRefinement(const QueryPattern& query,
                             const std::unordered_set<VertexID>& cover_set,
                             const std::unordered_set<VertexID>& independent_set,
                             std::vector<std::vector<VertexID> >& candidates,
                             ui max_iters = 20,
                             ui per_node_cap = 0) const;

    
    
    
    void setRelationNameMap(const std::vector<ui>& rel_q2k);

    
    void setQueryNodeLabels(const std::vector<std::unordered_set<LabelID> >& node_kg_labels);

    void setUndirectedMode(bool enabled);
    void setLogMode(LogMode mode);
    void setDebugMode(bool enabled);

    
    void setLabelInvertedIndex(const std::vector<std::vector<VertexID> >* idx);
    void setVamanaIndex(const VamanaANN* ann);

    
    
    
    
    const CandInitStats& getLastCandInitStats() const { return last_cand_init_stats_; }
    ui getLastRefineIters() const { return last_refine_iters_; }

private:
    struct DirectedConstraint {
        VertexID neighbor;
        ui relation_id;
        bool is_outgoing;
    };

    struct RelationDegreeRequirement {
        ui relation_id;
        ui out_required;
        ui in_required;
        ui any_required;
    };

private:
    bool relationMatch(ui query_rel, ui kg_rel) const;
    bool labelOk(VertexID query_node, VertexID kg_v) const;
    std::vector<RelationDegreeRequirement> buildRelationDegreeRequirements(
        VertexID query_node,
        const QueryPattern& query) const;
    bool relationDegreeOk(VertexID kg_v, const std::vector<RelationDegreeRequirement>& requirements) const;
    std::vector<DirectedConstraint> buildCrossSetConstraints(
        VertexID query_node,
        const QueryPattern& query,
        const std::unordered_set<VertexID>& opposite_set) const;

private:
    const EmbeddingDiGraph& kg_;
    bool undirected_mode_;
    LogMode log_mode_;
    std::vector<ui> rel_q2k_;
    std::vector<std::unordered_set<LabelID> > node_kg_labels_;
    const std::vector<std::vector<VertexID> >* label_to_vertices_ = NULL;
    const VamanaANN* ann_ = NULL;

    
    
    mutable CandInitStats last_cand_init_stats_;
    mutable ui last_refine_iters_ = 0;
};

#endif
