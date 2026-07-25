cmake_minimum_required(VERSION 3.10)
project(CFLfs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)
find_path(CEREAL_INCLUDE_DIR cereal/archives/json.hpp REQUIRED)

add_executable(cflfs_query
    core/StudyPerformance.cpp
    core/matchingcommand.cpp
    core/VertexLabelCompat.cpp
    core/FilterVertices.cpp
    core/GenerateFilteringPlan.cpp
    core/GenerateQueryPlan.cpp
    core/BuildTable.cpp
    core/EvaluateQuery.cpp
    graph/matching_graph.cpp
    graph/new_graph.cpp
    utility/QFilter.h
    utility/bitsetoperation.cpp
    utility/commandparser.cpp
    utility/computesetintersection.cpp
    utility/graphoperations.cpp
    utility/bi_matching/bigraphMatching.cpp
    utility/intersection/intersection_algos.cpp
    utility/intersection/utils/util.cpp
    utility/hashjoin/nop_join.cpp
    utility/leapfrogtriejoin/leapfrogtriejoin.cpp
    utility/primitive/nlf_filter.cpp
    utility/primitive/projection.cpp
    utility/primitive/scan.cpp
    utility/primitive/search.cpp
    utility/primitive/semi_join.cpp
    utility/relation/catalog.cpp
    utility/execution_tree/execution_tree.cpp
    utility/execution_tree/execution_tree_generator.cpp
    utility/execution_tree/execution_tree_node.cpp
    utility/nucleus_decomposition/bucket.cpp
    utility/nucleus_decomposition/hierarchy.cpp
    utility/nucleus_decomposition/k3-4.cpp
    utility/nucleus_decomposition/kcore.cpp
    utility/nucleus_decomposition/ktruss.cpp
    utility/nucleus_decomposition/nd_interface.cpp
    utility/nucleus_decomposition/util.cpp
)

target_include_directories(cflfs_query PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/utility
    ${CEREAL_INCLUDE_DIR}
)

target_compile_options(cflfs_query PRIVATE -O3 -march=native -pthread)
target_link_libraries(cflfs_query PRIVATE OpenMP::OpenMP_CXX)
