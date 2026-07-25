#ifndef SUBGRAPHMATCHING_NEW_GRAPH_H
#define SUBGRAPHMATCHING_NEW_GRAPH_H

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include "configuration/hash_map.h"
#include "configuration/types.h"

class EmbeddingDiGraph {
private:
    bool verbose_;
    bool enable_edge_index_;

    ui vertices_count_;
    ui edges_count_;
    ui relations_count_;
    ui labels_count_;
    ui max_out_degree_;
    ui max_in_degree_;
    ui max_total_degree_;
    ui max_label_frequency_;

    ui* out_offsets_;
    VertexID* out_neighbors_;
    ui* out_edge_ids_;

    ui* in_offsets_;
    VertexID* in_neighbors_;
    ui* in_edge_ids_;

    VertexID* edge_src_;
    VertexID* edge_dst_;
    ui* edge_relations_;

    ui* vertex_label_offsets_;
    LabelID* vertex_labels_;

    float* vertex_embeddings_;
    ui vertex_embedding_dim_;
    float* edge_embeddings_;
    ui edge_embedding_dim_;
    float* label_embeddings_;
    ui label_embedding_dim_;

    std::string* node_name_table_;
    std::string* relation_name_table_;
    std::string* type_name_table_;

    sparse_hash_map<uint64_t, std::vector<ui>* >* edge_index_;

private:
    void releaseGraph();
    void sortCSRByNeighborAndRelation(ui* offsets, VertexID* neighbors, ui* edge_ids, ui max_degree);
    void loadFromTextBinary(const std::string& folder_path, bool skip_vertex_type_files);
    void loadFromPickledKGFolder(const std::string& folder_path, bool skip_vertex_type_files);
    void loadFromCompactKGFolder(const std::string& folder_path, bool skip_vertex_type_files);

public:
    explicit EmbeddingDiGraph(bool verbose = false, bool enable_edge_index = true);
    ~EmbeddingDiGraph();

    
    void loadFromKG(const std::string& folder_path, bool skip_vertex_type_files = false);
    void printMetaData() const;
    void buildEdgeIndex();

public:
    inline ui getVerticesCount() const {
        return vertices_count_;
    }

    inline ui getEdgesCount() const {
        return edges_count_;
    }

    inline ui getRelationsCount() const {
        return relations_count_;
    }

    inline ui getLabelsCount() const {
        return labels_count_;
    }

    inline ui getMaxOutDegree() const {
        return max_out_degree_;
    }

    inline ui getMaxInDegree() const {
        return max_in_degree_;
    }

    inline ui getMaxTotalDegree() const {
        return max_total_degree_;
    }

    inline ui getMaxLabelFrequency() const {
        return max_label_frequency_;
    }

    inline ui getVertexEmbeddingDim() const {
        return vertex_embedding_dim_;
    }

    inline ui getEdgeEmbeddingDim() const {
        return edge_embedding_dim_;
    }

    inline ui getLabelEmbeddingDim() const {
        return label_embedding_dim_;
    }

    inline const VertexID* getOutNeighbors(const VertexID v, ui& count) const {
        count = out_offsets_[v + 1] - out_offsets_[v];
        return out_neighbors_ + out_offsets_[v];
    }

    inline const ui* getOutEdgeIds(const VertexID v, ui& count) const {
        count = out_offsets_[v + 1] - out_offsets_[v];
        return out_edge_ids_ + out_offsets_[v];
    }

    inline const VertexID* getInNeighbors(const VertexID v, ui& count) const {
        count = in_offsets_[v + 1] - in_offsets_[v];
        return in_neighbors_ + in_offsets_[v];
    }

    inline const ui* getInEdgeIds(const VertexID v, ui& count) const {
        count = in_offsets_[v + 1] - in_offsets_[v];
        return in_edge_ids_ + in_offsets_[v];
    }

    inline ui getEdgeRelationId(const ui edge_id) const {
        return edge_relations_[edge_id];
    }

    inline VertexID getEdgeSrc(const ui edge_id) const {
        return edge_src_[edge_id];
    }

    inline VertexID getEdgeDst(const ui edge_id) const {
        return edge_dst_[edge_id];
    }

    inline LabelID getVertexLabel(const VertexID v) const {
        if (vertex_label_offsets_ == NULL || vertex_label_offsets_[v] == vertex_label_offsets_[v + 1]) {
            return std::numeric_limits<LabelID>::max();
        }
        return vertex_labels_[vertex_label_offsets_[v]];
    }

    inline const LabelID* getVertexLabels(const VertexID v, ui& count) const {
        if (vertex_label_offsets_ == NULL) {
            count = 0;
            return NULL;
        }
        count = vertex_label_offsets_[v + 1] - vertex_label_offsets_[v];
        return vertex_labels_ + vertex_label_offsets_[v];
    }

    inline const float* getVertexEmbedding(const VertexID v) const {
        return vertex_embeddings_ + (size_t)v * vertex_embedding_dim_;
    }

    inline const float* getEdgeEmbeddingByRelation(const ui relation_id) const {
        return edge_embeddings_ + (size_t)relation_id * edge_embedding_dim_;
    }

    inline const float* getEdgeEmbeddingByEdgeId(const ui edge_id) const {
        ui relation_id = edge_relations_[edge_id];
        return edge_embeddings_ + (size_t)relation_id * edge_embedding_dim_;
    }

    inline const float* getLabelEmbedding(const LabelID label_id) const {
        if (label_embeddings_ == NULL || label_id >= labels_count_) {
            return NULL;
        }
        return label_embeddings_ + (size_t)label_id * label_embedding_dim_;
    }

    inline const std::string& getVertexName(const VertexID v) const {
        return node_name_table_[v];
    }

    inline const std::string& getRelationName(const LabelID relation_id) const {
        return relation_name_table_[relation_id];
    }

    inline const std::string& getTypeName(const LabelID label_id) const {
        return type_name_table_[label_id];
    }

    std::unordered_map<LabelID, const float*> getVertexLabelEmbeddings(const VertexID v) const;

    ui getEdgeId(const VertexID src, const VertexID dst, const ui relation_id) const;
    bool checkEdgeExistence(const VertexID src, const VertexID dst, const ui relation_id) const;
    const float* getEdgeEmbeddingByVertices(const VertexID src, const VertexID dst, const ui relation_id) const;
};

#endif
