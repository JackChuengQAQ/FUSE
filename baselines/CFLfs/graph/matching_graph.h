



#ifndef SUBGRAPHMATCHING_MATCHING_GRAPH_H
#define SUBGRAPHMATCHING_MATCHING_GRAPH_H

#include <unordered_map>
#include <iostream>
#include <vector>
#include "configuration/hash_map.h"
#include "configuration/types.h"
#include "configuration/config.h"

struct MatchingEdge {
    VertexID source;
    VertexID target;
    std::vector<LabelID> labels;
};

class Graph {
private:
    bool enable_vlabel_offset_;

    ui vertices_count_;
    ui edges_count_;
    ui vlabels_count_;
    ui max_degree_;
    ui max_vlabel_frequency_;

    ui* offsets_;
    VertexID * neighbors_;
    LabelID* vlabels_;
    ui* reverse_index_offsets_;
    ui* reverse_index_;

    int* core_table_;
    ui core_length_;

    std::unordered_map<LabelID, ui> vlabels_frequency_;
    sparse_hash_map<uint64_t, std::vector<edge>* >* edge_index_;

#if OPTIMIZED_VLABELED_GRAPH == 1
    ui* vlabels_offsets_;

#ifdef ELABELED_GRAPH
    
    std::unordered_map<LabelID, std::unordered_map<LabelID, ui>>*nlf_;
#else
    
    std::unordered_map<LabelID, ui>* nlf_;
#endif

#endif

#ifdef ELABELED_GRAPH
    ui elabels_count_;
    
    
    
    ui* elabels_begin_;
    ui* elabels_len_;
    LabelID* elabels_data_;
#endif

private:
    void BuildReverseIndex();

#if OPTIMIZED_VLABELED_GRAPH == 1
    void BuildNLF();
    void BuildVLabelOffset();
#endif

public:
    Graph(const bool enable_label_offset) {
        enable_vlabel_offset_ = enable_label_offset;

        vertices_count_ = 0;
        edges_count_ = 0;
        vlabels_count_ = 0;
        max_degree_ = 0;
        max_vlabel_frequency_ = 0;
        core_length_ = 0;

        offsets_ = NULL;
        neighbors_ = NULL;
        vlabels_ = NULL;
        reverse_index_offsets_ = NULL;
        reverse_index_ = NULL;
        core_table_ = NULL;
        vlabels_frequency_.clear();
        edge_index_ = NULL;
#if OPTIMIZED_VLABELED_GRAPH == 1
        vlabels_offsets_ = NULL;
        nlf_ = NULL;
#endif

#ifdef ELABELED_GRAPH
        elabels_count_ = 0;
        elabels_begin_ = NULL;
        elabels_len_ = NULL;
        elabels_data_ = NULL;
#endif
    }

    ~Graph() {
        delete[] offsets_;
        delete[] neighbors_;
        delete[] vlabels_;
        delete[] reverse_index_offsets_;
        delete[] reverse_index_;
        delete[] core_table_;
        delete edge_index_;
#if OPTIMIZED_VLABELED_GRAPH == 1
        delete[] vlabels_offsets_;
        delete[] nlf_;
#endif

#ifdef ELABELED_GRAPH
        delete[] elabels_begin_;
        delete[] elabels_len_;
        delete[] elabels_data_;
#endif
    }

public:
    void buildFromMemory(const std::vector<LabelID>& vertex_labels,
                         const std::vector<MatchingEdge>& edges);
    void printGraphMetaData();
public:
    const ui getLabelsCount() const {
        return vlabels_count_;
    }

    const ui getVerticesCount() const {
        return vertices_count_;
    }

    const ui getEdgesCount() const {
        return edges_count_;
    }

    const ui getGraphMaxDegree() const {
        return max_degree_;
    }

    const ui getGraphMaxLabelFrequency() const {
        return max_vlabel_frequency_;
    }

    const ui getVertexDegree(const VertexID id) const {
        return offsets_[id + 1] - offsets_[id];
    }

    const ui getLabelsFrequency(const LabelID label) const {
        return vlabels_frequency_.find(label) == vlabels_frequency_.end() ? 0 : vlabels_frequency_.at(label);
    }

    const ui getCoreValue(const VertexID id) const {
        return core_table_[id];
    }

    const ui get2CoreSize() const {
        return core_length_;
    }
    const LabelID getVertexLabel(const VertexID id) const {
        return vlabels_[id];
    }

    const ui * getVertexNeighbors(const VertexID id, ui& count) const {
        count = offsets_[id + 1] - offsets_[id];
        return neighbors_ + offsets_[id];
    }

    const sparse_hash_map<uint64_t, std::vector<edge>*>* getEdgeIndex() const {
        return edge_index_;
    }

    const ui * getVerticesByLabel(const LabelID id, ui& count) const {
        count = reverse_index_offsets_[id + 1] - reverse_index_offsets_[id];
        return reverse_index_ + reverse_index_offsets_[id];
    }

#if OPTIMIZED_VLABELED_GRAPH == 1
    const ui * getNeighborsByLabel(const VertexID id, const LabelID label, ui& count) const {
        ui offset = id * vlabels_count_ + label;
        count = vlabels_offsets_[offset + 1] - vlabels_offsets_[offset];
        return neighbors_ + vlabels_offsets_[offset];
    }

#ifdef ELABELED_GRAPH
    const std::unordered_map<LabelID, std::unordered_map<LabelID, ui>>* getVertexNLF(const VertexID id) const {
        return nlf_ + id;
    }
    bool checkEdgeExistence(const VertexID u, const VertexID v, const LabelID u_label, const LabelID e_label) const {
        ui count = 0;
        const VertexID* neighbors = getNeighborsByLabel(v, u_label, count);
        int begin = 0;
        int end = count - 1;
        while (begin <= end) {
            int mid = begin + ((end - begin) >> 1);
            if (neighbors[mid] == u) {
                return edgeHasLabelAt(v, (ui)mid, e_label);
            }
            else if (neighbors[mid] > u)
                end = mid - 1;
            else
                begin = mid + 1;
        }

        return false;
    }
#else
    const std::unordered_map<LabelID, ui>* getVertexNLF(const VertexID id) const {
        return nlf_ + id;
    }
    bool checkEdgeExistence(const VertexID u, const VertexID v, const LabelID u_label) const {
        ui count = 0;
        const VertexID* neighbors = getNeighborsByLabel(v, u_label, count);
        int begin = 0;
        int end = count - 1;
        while (begin <= end) {
            int mid = begin + ((end - begin) >> 1);
            if (neighbors[mid] == u) {
                return true;
            }
            else if (neighbors[mid] > u)
                end = mid - 1;
            else
                begin = mid + 1;
        }

        return false;
    }
#endif

#endif

#ifdef ELABELED_GRAPH
    
    LabelID getEdgeLabel(VertexID s, VertexID e, bool non_sense) const {
        ui nbrs_num = 0;
        const ui* nbrs = getVertexNeighbors(s, nbrs_num);
        for (ui i = 0; i < nbrs_num; i++) {
            if (nbrs[i] == e) {
                ui cnt = 0;
                const LabelID* labels = getEdgeLabelList(s, i, cnt);
                return cnt > 0 ? labels[0] : (LabelID)-1;
            }
        }
        return (LabelID)-1;
    }
    LabelID getEdgeLabelCheck(VertexID s, VertexID e, ui offset) const {
        if ((neighbors_+offsets_[s])[offset] == e) {
            ui cnt = 0;
            const LabelID* labels = getEdgeLabelList(s, offset, cnt);
            return cnt > 0 ? labels[0] : (LabelID)-1;
        } else {
            std::cout << "error in edge label check" << std::endl;
        }
    }
    
    inline LabelID getEdgeLabel(VertexID s, ui offset) const {
        ui p = offsets_[s] + offset;
        ui begin = elabels_begin_[p];
        ui len = elabels_len_[p];
        return len > 0 ? elabels_data_[begin] : (LabelID)-1;
    }
    
    
    inline const LabelID* getEdgeLabelList(const VertexID id, ui nbr_local_idx, ui& label_count) const {
        ui p = offsets_[id] + nbr_local_idx;
        ui begin = elabels_begin_[p];
        ui len = elabels_len_[p];
        label_count = len;
        return elabels_data_ + begin;
    }

    inline bool edgeHasLabelAt(const VertexID id, ui nbr_local_idx, const LabelID label) const {
        ui cnt = 0;
        const LabelID* labels = getEdgeLabelList(id, nbr_local_idx, cnt);
        
        int l = 0, r = (int)cnt - 1;
        while (l <= r) {
            int m = l + ((r - l) >> 1);
            LabelID mid = labels[m];
            if (mid == label) return true;
            if (mid < label) l = m + 1;
            else r = m - 1;
        }
        return false;
    }

    bool checkEdgeExistence(VertexID u, VertexID v, const LabelID e_label) const {
        if (getVertexDegree(u) < getVertexDegree(v)) {
            std::swap(u, v);
        }
        ui count = 0;
        const VertexID* neighbors =  getVertexNeighbors(v, count);

        int begin = 0;
        int end = count - 1;
        while (begin <= end) {
            int mid = begin + ((end - begin) >> 1);
            if (neighbors[mid] == u) {
                return edgeHasLabelAt(v, (ui)mid, e_label);
            }
            else if (neighbors[mid] > u)
                end = mid - 1;
            else
                begin = mid + 1;
        }

        return false;
    }

#endif

    bool checkEdgeExistence(VertexID u, VertexID v) const {
        if (getVertexDegree(u) < getVertexDegree(v)) {
            std::swap(u, v);
        }
        ui count = 0;
        const VertexID* neighbors =  getVertexNeighbors(v, count);

        int begin = 0;
        int end = count - 1;
        while (begin <= end) {
            int mid = begin + ((end - begin) >> 1);
            if (neighbors[mid] == u) {
                return true;
            }
            else if (neighbors[mid] > u)
                end = mid - 1;
            else
                begin = mid + 1;
        }

        return false;
    }

    void buildCoreTable();

    void buildEdgeIndex();
};


#endif 
