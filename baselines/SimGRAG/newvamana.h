#ifndef SUBGRAPHMATCHING_BASELINE_NEWVAMANA_H
#define SUBGRAPHMATCHING_BASELINE_NEWVAMANA_H

#include <mutex>
#include <string>
#include <vector>

#include "configuration/types.h"
#include "new_graph.h"
#include "vamana_search_queue.h"



class VamanaANN {
public:
    static constexpr float kGraphSlackFactor = 1.3f;

    struct BuildConfig {
        ui max_degree;
        ui alpha_scaled_by_1000;
        ui max_candidate_size;
        ui sample_size_for_entry;
        ui Lbuild;
        ui num_build_threads;
        
        
        bool use_full_vamana_link;

        BuildConfig()
            : max_degree(32),
              alpha_scaled_by_1000(1200),
              max_candidate_size(750),
              sample_size_for_entry(512),
              Lbuild(128),
              num_build_threads(1),
              use_full_vamana_link(false) {
        }
    };

    struct SearchConfig {
        ui ef_search;
        
        
        bool use_graph_search;

        SearchConfig() : ef_search(128), use_graph_search(false) {
        }
    };

    struct SearchResult {
        VertexID id;
        float distance;
    };

public:
    explicit VamanaANN(bool verbose = false);

    
    static ui computeNaiveLsearch(ui node_sim_topk);

    void build(const EmbeddingDiGraph& graph, const BuildConfig& config = BuildConfig());
    
    bool buildOrLoad(const EmbeddingDiGraph& graph, const BuildConfig& config, const std::string& index_dir);
    void save(const std::string& index_dir) const;
    bool load(const EmbeddingDiGraph& graph, const std::string& index_dir);
    static bool indexExists(const std::string& index_dir);

    std::vector<SearchResult> search(const float* query_embedding, ui top_k,
                                     const SearchConfig& config = SearchConfig()) const;
    std::vector<SearchResult> searchByVertex(VertexID query_vertex, ui top_k,
                                             const SearchConfig& config = SearchConfig()) const;

    inline VertexID getEntryPoint() const {
        return entry_point_;
    }

    inline ui getDimension() const {
        return dim_;
    }

private:
    struct Candidate {
        VertexID id;
        float distance;
    };

    float l2Distance(const float* lhs, const float* rhs) const;
    VertexID chooseMedoid() const;
    VertexID chooseEntryPoint(ui sample_size) const;
    std::vector<VertexID> collectDirectedNeighbors(VertexID vertex) const;
    void pruneNeighbors(VertexID center, std::vector<Candidate>& candidates, std::vector<VertexID>& output,
                        std::vector<float>& occlude) const;
    void trimVertexNeighbors(VertexID center, std::vector<float>& occlude_scratch);
    void addReverseEdgesAndTrim(std::vector<float>& occlude_scratch);
    void buildFromKgNeighbors(std::vector<float>& occlude_scratch);
    void link(ui Lbuild, ui num_threads);
    std::vector<SearchResult> searchBruteForce(const float* query_embedding, ui top_k) const;
    std::vector<SearchResult> searchOnGraph(const float* query_embedding, ui top_k, ui Lsearch) const;
    void interInsert(VertexID src, const std::vector<VertexID>& src_neighbors, std::vector<float>& occlude_scratch,
                     std::mutex* neighbor_locks);
    ui iterateToFixedPoint(const float* query, ui list_size, VamanaSearchScratch& scratch, bool record_expanded,
                           VertexID target_id, std::mutex* neighbor_locks) const;
    static void collectTopKFromQueue(const VamanaSearchScratch& scratch, ui top_k, std::vector<SearchResult>& out);

private:
    const EmbeddingDiGraph* graph_;
    bool verbose_;
    ui n_;
    ui dim_;
    VertexID entry_point_;
    float alpha_;
    ui max_degree_;
    ui max_candidate_size_;
    std::vector<std::vector<VertexID> > graph_index_;
};

#endif
