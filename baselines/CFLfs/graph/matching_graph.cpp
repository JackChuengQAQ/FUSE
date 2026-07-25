



#include "matching_graph.h"
#include <vector>
#include <algorithm>
#include <limits>
#include <utility/graphoperations.h>

void Graph::BuildReverseIndex() {
    reverse_index_ = new ui[vertices_count_];
    reverse_index_offsets_= new ui[vlabels_count_ + 1];
    reverse_index_offsets_[0] = 0;

    ui total = 0;
    for (ui i = 0; i < vlabels_count_; ++i) {
        reverse_index_offsets_[i + 1] = total;
        total += vlabels_frequency_[i];
    }

    for (ui i = 0; i < vertices_count_; ++i) {
        LabelID vlabel = vlabels_[i];
        reverse_index_[reverse_index_offsets_[vlabel + 1]++] = i;
    }
}

#if OPTIMIZED_VLABELED_GRAPH == 1

#ifdef ELABELED_GRAPH
void Graph::BuildNLF() {
    nlf_ = new std::unordered_map<LabelID, std::unordered_map<LabelID, ui>>[vertices_count_];
    for (ui i = 0; i < vertices_count_; ++i) {
        ui count;
        const VertexID* neighbors = getVertexNeighbors(i, count);

        for (ui j = 0; j < count; ++j) {
            VertexID u = neighbors[j];
            LabelID vlabel = getVertexLabel(u);
            ui lcnt = 0;
            const LabelID* labels = getEdgeLabelList(i, j, lcnt);
            for (ui k = 0; k < lcnt; ++k) {
                LabelID elabel = labels[k];
                auto nlf = nlf_[i].find(vlabel);
                if (nlf == nlf_[i].end() || nlf->second.find(elabel) == nlf->second.end()) {
                    nlf_[i][vlabel][elabel] = 1;
                } else {
                    nlf_[i][vlabel][elabel]++;
                }
            }
        }
    }
}
#else
void Graph::BuildNLF() {
    nlf_ = new std::unordered_map<LabelID, ui>[vertices_count_];
    for (ui i = 0; i < vertices_count_; ++i) {
        ui count;
        const VertexID * neighbors = getVertexNeighbors(i, count);

        for (ui j = 0; j < count; ++j) {
            VertexID u = neighbors[j];
            LabelID vlabel = getVertexLabel(u);
            nlf_[i][vlabel] += 1;
        }
    }
}
#endif

void Graph::BuildVLabelOffset() {
    size_t vlabels_offset_size = (size_t)vertices_count_ * vlabels_count_ + 1;
    vlabels_offsets_ = new ui[vlabels_offset_size];
    std::fill(vlabels_offsets_, vlabels_offsets_ + vlabels_offset_size, 0);

    for (ui i = 0; i < vertices_count_; ++i) {
        std::sort(neighbors_ + offsets_[i], neighbors_ + offsets_[i + 1],
            [this](const VertexID u, const VertexID v) -> bool {
                return vlabels_[u] == vlabels_[v] ? u < v : vlabels_[u] < vlabels_[v];
            });
    }

    for (ui i = 0; i < vertices_count_; ++i) {
        LabelID previous_vlabel = 0;
        LabelID current_vlabel = 0;

        vlabels_offset_size = i * vlabels_count_;
        vlabels_offsets_[vlabels_offset_size] = offsets_[i];

        for (ui j = offsets_[i]; j < offsets_[i + 1]; ++j) {
            current_vlabel = vlabels_[neighbors_[j]];

            if (current_vlabel != previous_vlabel) {
                for (ui k = previous_vlabel + 1; k <= current_vlabel; ++k) {
                    vlabels_offsets_[vlabels_offset_size + k] = j;
                }
                previous_vlabel = current_vlabel;
            }
        }

        for (ui l = current_vlabel + 1; l <= vlabels_count_; ++l) {
            vlabels_offsets_[vlabels_offset_size + l] = offsets_[i + 1];
        }
    }
}

#endif

void Graph::buildFromMemory(const std::vector<LabelID>& vertex_labels,
                            const std::vector<MatchingEdge>& edges) {
    if (offsets_ != NULL || neighbors_ != NULL || vlabels_ != NULL) {
        std::cerr << "Matching graph is already initialized." << std::endl;
        std::exit(-1);
    }
    if (vertex_labels.size() > static_cast<size_t>(std::numeric_limits<ui>::max()) ||
        edges.size() > static_cast<size_t>(std::numeric_limits<ui>::max())) {
        std::cerr << "Matching graph exceeds the supported size." << std::endl;
        std::exit(-1);
    }

    vertices_count_ = static_cast<ui>(vertex_labels.size());
    edges_count_ = static_cast<ui>(edges.size());
    offsets_ = new ui[static_cast<size_t>(vertices_count_) + 1]();
    vlabels_ = new LabelID[vertices_count_];

    bool has_vertex_label = false;
    LabelID max_vertex_label = 0;
    for (ui vertex = 0; vertex < vertices_count_; ++vertex) {
        LabelID label = vertex_labels[vertex];
        vlabels_[vertex] = label;
        vlabels_frequency_[label] += 1;
        if (!has_vertex_label || label > max_vertex_label) {
            max_vertex_label = label;
        }
        has_vertex_label = true;
    }
    vlabels_count_ = has_vertex_label ? max_vertex_label + 1 : 0;
    for (const auto& item : vlabels_frequency_) {
        if (item.second > max_vlabel_frequency_) {
            max_vlabel_frequency_ = item.second;
        }
    }

    std::vector<ui> degrees(vertices_count_, 0);
    uint64_t previous_key = 0;
    bool has_previous_key = false;
    for (const MatchingEdge& item : edges) {
        if (item.source >= vertices_count_ || item.target >= vertices_count_ ||
            item.source >= item.target) {
            std::cerr << "Invalid matching edge." << std::endl;
            std::exit(-1);
        }
        uint64_t key = (static_cast<uint64_t>(item.source) << 32) | item.target;
        if (has_previous_key && key <= previous_key) {
            std::cerr << "Matching edges are not strictly ordered." << std::endl;
            std::exit(-1);
        }
        previous_key = key;
        has_previous_key = true;
        degrees[item.source] += 1;
        degrees[item.target] += 1;
    }

    for (ui vertex = 0; vertex < vertices_count_; ++vertex) {
        offsets_[vertex + 1] = offsets_[vertex] + degrees[vertex];
        if (degrees[vertex] > max_degree_) {
            max_degree_ = degrees[vertex];
        }
    }

    size_t adjacency_count = static_cast<size_t>(edges_count_) * 2;
    neighbors_ = new VertexID[adjacency_count];
    std::vector<ui> cursors(offsets_, offsets_ + vertices_count_);

#ifdef ELABELED_GRAPH
    elabels_begin_ = new ui[adjacency_count]();
    elabels_len_ = new ui[adjacency_count]();
    std::vector<LabelID> label_data;
    label_data.reserve(adjacency_count);
    bool has_edge_label = false;
    LabelID max_edge_label = 0;
#endif

    for (const MatchingEdge& item : edges) {
        ui source_position = cursors[item.source]++;
        ui target_position = cursors[item.target]++;
        neighbors_[source_position] = item.target;
        neighbors_[target_position] = item.source;

#ifdef ELABELED_GRAPH
        std::vector<LabelID> labels = item.labels;
        std::sort(labels.begin(), labels.end());
        labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
        auto append_labels = [&](ui position) {
            elabels_begin_[position] = static_cast<ui>(label_data.size());
            elabels_len_[position] = static_cast<ui>(labels.size());
            for (LabelID label : labels) {
                label_data.push_back(label);
                if (!has_edge_label || label > max_edge_label) {
                    max_edge_label = label;
                }
                has_edge_label = true;
            }
        };
        append_labels(source_position);
        append_labels(target_position);
#endif
    }

#ifdef ELABELED_GRAPH
    elabels_data_ = new LabelID[label_data.size()];
    std::copy(label_data.begin(), label_data.end(), elabels_data_);
    elabels_count_ = has_edge_label ? max_edge_label + 1 : 0;
#endif

    BuildReverseIndex();
    buildEdgeIndex();

#if OPTIMIZED_VLABELED_GRAPH == 1
    if (enable_vlabel_offset_) {
        BuildNLF();
    }
#endif
}

void Graph::printGraphMetaData() {
    std::cout << "|V|: " << vertices_count_ << ", |E|: " << edges_count_ << ", |\u03A3|: " << vlabels_count_ << std::endl;
    std::cout << "Max Degree: " << max_degree_ << ", Max Label Frequency: " << max_vlabel_frequency_ << std::endl;
#ifdef ELABELED_GRAPH
    std::cout << "#Edge Label: " << elabels_count_ << std::endl;
#endif
}

void Graph::buildCoreTable() {
    core_table_ = new int[vertices_count_];
    GraphOperations::getKCore(this, core_table_);

    for (ui i = 0; i < vertices_count_; ++i) {
        if (core_table_[i] > 1) {
            core_length_ += 1;
        }
    }
}

void Graph::buildEdgeIndex() {
    edge_index_ = new sparse_hash_map<uint64_t, std::vector<edge>*>();

    edge cur_edge;
    for (uint32_t u = 0; u < vertices_count_; ++u) {
        uint32_t u_l = getVertexLabel(u);

        uint32_t u_nbrs_cnt;
        const uint32_t* u_nbrs = getVertexNeighbors(u, u_nbrs_cnt);

        cur_edge.vertices_[0] = u;

        for (uint32_t i = 0; i < u_nbrs_cnt; ++i) {
            uint32_t v = u_nbrs[i];
            uint32_t v_l = getVertexLabel(v);

            uint64_t key = (uint64_t) u_l << 32 | v_l;
            cur_edge.vertices_[1] = v;

            if (edge_index_->find(key) == edge_index_->end()) {
                (*edge_index_)[key] = new std::vector<edge>();
            }
            (*edge_index_)[key]->emplace_back(cur_edge);
        }
    }

}
