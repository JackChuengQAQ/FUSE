cmake_minimum_required(VERSION 3.10)
project(Fuse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(fuse STATIC
    cover_refinement.cpp
    global_search.cpp
    newvamana.cpp
    upper_bound_pruning.cpp
    graph/new_graph.cpp
)

target_include_directories(fuse PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/configuration
    ${CMAKE_CURRENT_SOURCE_DIR}/graph
)

target_compile_options(fuse PRIVATE -O3 -march=native -pthread)

find_package(OpenMP REQUIRED)
target_link_libraries(fuse PUBLIC OpenMP::OpenMP_CXX)

add_executable(fuse_query main.cpp)
target_compile_options(fuse_query PRIVATE -O3 -march=native -pthread)
target_link_libraries(fuse_query PRIVATE fuse)
