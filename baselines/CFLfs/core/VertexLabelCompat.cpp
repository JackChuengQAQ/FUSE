#include "VertexLabelCompat.h"

namespace VertexLabelCompat {

namespace {

const std::vector<std::vector<LabelID>>* g_query_types = nullptr;
const std::vector<std::vector<LabelID>>* g_data_types = nullptr;
const std::vector<std::vector<VertexID>>* g_data_inv = nullptr;
bool g_intersection = false;
bool g_ignore_vertex_labels = false;

}  

void reset() {
    g_query_types = nullptr;
    g_data_types = nullptr;
    g_data_inv = nullptr;
    g_intersection = false;
    g_ignore_vertex_labels = false;
}

void enableIgnoreVertexLabels() {
    g_ignore_vertex_labels = true;
}

bool isIgnoreVertexLabels() {
    return g_ignore_vertex_labels;
}

ui maxCandidateBufferSize(const Graph* data_graph) {
    if (data_graph == nullptr) {
        return 0;
    }
    if (isIgnoreVertexLabels()) {
        return data_graph->getVerticesCount();
    }
    return data_graph->getGraphMaxLabelFrequency();
}

void enableIntersectionMode(const std::vector<std::vector<LabelID>>* query_vertex_types,
                            const std::vector<std::vector<LabelID>>* data_vertex_types) {
    g_query_types = query_vertex_types;
    g_data_types = data_vertex_types;
    g_intersection = true;
}

void setDataInvertedIndex(const std::vector<std::vector<VertexID>>* label_to_data_vertices) {
    g_data_inv = label_to_data_vertices;
}

const std::vector<std::vector<VertexID>>* getDataInvertedIndex() {
    return g_data_inv;
}

const std::vector<std::vector<LabelID>>* getQueryVertexTypes() {
    return g_query_types;
}

const std::vector<std::vector<LabelID>>* getDataVertexTypes() {
    return g_data_types;
}

bool isIntersectionMode() {
    return g_intersection && g_query_types != nullptr && g_data_types != nullptr && g_data_inv != nullptr;
}

bool typesIntersectSorted(const std::vector<LabelID>& a, const std::vector<LabelID>& b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            return true;
        }
        if (a[i] < b[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return false;
}

bool vertexMatches(const Graph* query_graph, VertexID qu, const Graph* data_graph, VertexID dv) {
    (void)query_graph;
    (void)qu;
    (void)data_graph;
    (void)dv;
    if (isIgnoreVertexLabels()) {
        return true;
    }
    if (!isIntersectionMode()) {
        return query_graph->getVertexLabel(qu) == data_graph->getVertexLabel(dv);
    }
    return typesIntersectSorted((*g_query_types)[qu], (*g_data_types)[dv]);
}

}  
