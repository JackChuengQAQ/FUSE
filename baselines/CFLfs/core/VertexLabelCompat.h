#pragma once

#include <vector>
#include "graph/matching_graph.h"




namespace VertexLabelCompat {

void reset();

void enableIgnoreVertexLabels();

bool isIgnoreVertexLabels();


ui maxCandidateBufferSize(const Graph* data_graph);

void enableIntersectionMode(const std::vector<std::vector<LabelID>>* query_vertex_types,
                            const std::vector<std::vector<LabelID>>* data_vertex_types);

void setDataInvertedIndex(const std::vector<std::vector<VertexID>>* label_to_data_vertices);

const std::vector<std::vector<VertexID>>* getDataInvertedIndex();

const std::vector<std::vector<LabelID>>* getQueryVertexTypes();

const std::vector<std::vector<LabelID>>* getDataVertexTypes();

bool isIntersectionMode();

bool typesIntersectSorted(const std::vector<LabelID>& a, const std::vector<LabelID>& b);

bool vertexMatches(const Graph* query_graph, VertexID qu, const Graph* data_graph, VertexID dv);

}  
