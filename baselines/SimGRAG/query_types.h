#ifndef SIMGRAG_QUERY_TYPES_H
#define SIMGRAG_QUERY_TYPES_H

#include <vector>

#include "configuration/types.h"

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
    std::vector<std::vector<QueryNeighborInfo>> out_neighbors;
    std::vector<std::vector<QueryNeighborInfo>> in_neighbors;
};

#endif
