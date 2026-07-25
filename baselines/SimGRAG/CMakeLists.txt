cmake_minimum_required(VERSION 3.10)
project(SimGRAG LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(simgrag STATIC
    naive_greedy.cpp
    newvamana.cpp
    graph/new_graph.cpp
)

target_include_directories(simgrag PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/configuration
    ${CMAKE_CURRENT_SOURCE_DIR}/graph
)

target_compile_options(simgrag PRIVATE -O3 -march=native -pthread)

find_package(OpenMP REQUIRED)
target_link_libraries(simgrag PUBLIC OpenMP::OpenMP_CXX)

add_executable(simgrag_query main.cpp)
target_compile_options(simgrag_query PRIVATE -O3 -march=native -pthread)
target_link_libraries(simgrag_query PRIVATE simgrag)
