



#include <chrono>
#include <future>
#include <thread>
#include <fstream>
#include <cstdio>
#include <unordered_map>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cctype>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <limits>

#include "matchingcommand.h"
#include "graph/new_graph.h"
#include "graph/matching_graph.h"
#include "GenerateFilteringPlan.h"
#include "FilterVertices.h"
#include "BuildTable.h"
#include "GenerateQueryPlan.h"
#include "EvaluateQuery.h"
#include "VertexLabelCompat.h"

#define NANOSECTOSEC(elapsed_time) ((elapsed_time)/(double)1000000000)
#define BYTESTOMB(memory_cost) ((memory_cost)/(double)(1024 * 1024))

size_t enumerate(Graph* data_graph, Graph* query_graph, Edges*** edge_matrix, ui** candidates, ui* candidates_count,
                ui* matching_order, size_t output_limit) {
    static ui order_id = 0;

    order_id += 1;

    auto start = std::chrono::high_resolution_clock::now();
    size_t call_count = 0;
    size_t valid_vtx_count = 0;
    size_t embedding_count = EvaluateQuery::LFTJ(data_graph, query_graph, edge_matrix, candidates, candidates_count,
                               matching_order, output_limit, call_count, valid_vtx_count);

    auto end = std::chrono::high_resolution_clock::now();
    double enumeration_time_in_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
#ifdef SPECTRUM
    if (EvaluateQuery::exit_) {
        printf("Spectrum Order %u status: Timeout\n", order_id);
    }
    else {
        printf("Spectrum Order %u status: Complete\n", order_id);
    }
#endif
    printf("Spectrum Order %u Enumerate time (seconds): %.4lf\n", order_id, NANOSECTOSEC(enumeration_time_in_ns));
    printf("Spectrum Order %u #Embeddings: %zu\n", order_id, embedding_count);
    printf("Spectrum Order %u Call Count: %zu\n", order_id, call_count);
    printf("Spectrum Order %u Per Call Count Time (nanoseconds): %.4lf\n", order_id, enumeration_time_in_ns / (call_count == 0 ? 1 : call_count));

    return embedding_count;
}

void spectrum_analysis(Graph* data_graph, Graph* query_graph, Edges*** edge_matrix, ui** candidates, ui* candidates_count,
                       size_t output_limit, std::vector<std::vector<ui>>& spectrum, size_t time_limit_in_sec) {

    for (auto& order : spectrum) {
        std::cout << "----------------------------" << std::endl;
        ui* matching_order = order.data();
        GenerateQueryPlan::printSimplifiedQueryPlan(query_graph, matching_order);

        std::future<size_t> future = std::async(std::launch::async, [data_graph, query_graph, edge_matrix, candidates, candidates_count,
                                                                     matching_order, output_limit](){
            return enumerate(data_graph, query_graph, edge_matrix, candidates, candidates_count, matching_order, output_limit);
        });

        std::cout << "execute...\n";
        std::future_status status;
        do {
            status = future.wait_for(std::chrono::seconds(time_limit_in_sec));
            if (status == std::future_status::deferred) {
                std::cout << "Deferred\n";
                exit(-1);
            } else if (status == std::future_status::timeout) {
#ifdef SPECTRUM
                EvaluateQuery::exit_ = true;
#endif
            }
        } while (status != std::future_status::ready);
    }
}


static LabelID nextAvailableLabelId(const std::unordered_map<std::string, LabelID>& name_to_label) {
    LabelID next = 0;
    for (const auto& kv : name_to_label) {
        if (kv.second >= next) {
            next = kv.second + 1;
        }
    }
    return next;
}

static void buildUnifiedRelationNameLabelMapFromDataOnly(const EmbeddingDiGraph& data_kg,
                                                         std::unordered_map<std::string, LabelID>& name_to_label) {
    name_to_label.clear();
    std::set<std::string> names;
    for (ui i = 0; i < data_kg.getRelationsCount(); ++i) {
        names.insert(data_kg.getRelationName(i));
    }
    LabelID id = 0;
    for (const auto& nm : names) {
        name_to_label[nm] = id++;
    }
}

static void appendQueryOnlyRelationNames(const EmbeddingDiGraph& query_kg,
                                         std::unordered_map<std::string, LabelID>& name_to_label) {
    std::set<std::string> extra;
    for (ui i = 0; i < query_kg.getRelationsCount(); ++i) {
        const std::string& nm = query_kg.getRelationName(i);
        if (name_to_label.find(nm) == name_to_label.end()) {
            extra.insert(nm);
        }
    }
    LabelID id = nextAvailableLabelId(name_to_label);
    for (const auto& nm : extra) {
        name_to_label[nm] = id++;
    }
}


static void buildUnifiedRelationNameLabelMapDataFirst(const EmbeddingDiGraph& data_kg,
                                                      const EmbeddingDiGraph& query_kg,
                                                      std::unordered_map<std::string, LabelID>& name_to_label) {
    buildUnifiedRelationNameLabelMapFromDataOnly(data_kg, name_to_label);
    appendQueryOnlyRelationNames(query_kg, name_to_label);
}

static void buildUnifiedTypeNameLabelMapFromDataOnly(const EmbeddingDiGraph& data_kg,
                                                     std::unordered_map<std::string, LabelID>& name_to_label) {
    name_to_label.clear();
    std::set<std::string> names;
    for (ui i = 0; i < data_kg.getLabelsCount(); ++i) {
        names.insert(data_kg.getTypeName(i));
    }
    LabelID id = 0;
    for (const auto& nm : names) {
        name_to_label[nm] = id++;
    }
}

static void appendQueryOnlyTypeNames(const EmbeddingDiGraph& query_kg,
                                     std::unordered_map<std::string, LabelID>& name_to_label) {
    std::set<std::string> extra;
    for (ui i = 0; i < query_kg.getLabelsCount(); ++i) {
        const std::string& tn = query_kg.getTypeName(i);
        if (name_to_label.find(tn) == name_to_label.end()) {
            extra.insert(tn);
        }
    }
    LabelID id = nextAvailableLabelId(name_to_label);
    for (const auto& nm : extra) {
        name_to_label[nm] = id++;
    }
}

static void buildUnifiedTypeNameLabelMapDataFirst(const EmbeddingDiGraph& data_kg,
                                                  const EmbeddingDiGraph& query_kg,
                                                  std::unordered_map<std::string, LabelID>& name_to_label) {
    buildUnifiedTypeNameLabelMapFromDataOnly(data_kg, name_to_label);
    appendQueryOnlyTypeNames(query_kg, name_to_label);
}


static std::vector<LabelID> computeSortedUnifiedTypesForVertex(
    const EmbeddingDiGraph& g, VertexID v,
    const std::unordered_map<std::string, LabelID>& type_name_to_label) {
    std::vector<LabelID> key;
    ui cnt = 0;
    const LabelID* lbs = g.getVertexLabels(v, cnt);
    for (ui i = 0; i < cnt; ++i) {
        const std::string& tn = g.getTypeName(lbs[i]);
        auto it = type_name_to_label.find(tn);
        if (it == type_name_to_label.end()) {
            std::cerr << "Type name missing from unified type map: " << tn << std::endl;
            exit(-1);
        }
        key.push_back(it->second);
    }
    std::sort(key.begin(), key.end());
    key.erase(std::unique(key.begin(), key.end()), key.end());
    return key;
}

static void buildPerVertexUnifiedTypeVectors(
    const EmbeddingDiGraph& query_kg,
    const EmbeddingDiGraph& data_kg,
    const std::unordered_map<std::string, LabelID>& type_name_to_label,
    std::vector<std::vector<LabelID>>& query_types,
    std::vector<std::vector<LabelID>>& data_types) {
    query_types.resize(query_kg.getVerticesCount());
    for (ui v = 0; v < query_kg.getVerticesCount(); ++v) {
        query_types[v] = computeSortedUnifiedTypesForVertex(query_kg, v, type_name_to_label);
    }
    data_types.resize(data_kg.getVerticesCount());
    for (ui v = 0; v < data_kg.getVerticesCount(); ++v) {
        data_types[v] = computeSortedUnifiedTypesForVertex(data_kg, v, type_name_to_label);
    }
}

static void buildDataVertexInvertedIndexByUnifiedType(
    const std::vector<std::vector<LabelID>>& data_types,
    LabelID max_type_id,
    std::vector<std::vector<VertexID>>& inv) {
    inv.assign(static_cast<size_t>(max_type_id) + 1, {});
    for (VertexID v = 0; v < static_cast<VertexID>(data_types.size()); ++v) {
        for (LabelID t : data_types[v]) {
            inv[t].push_back(v);
        }
    }
}



static void buildCompositeVertexLabelVectors(const EmbeddingDiGraph& query_kg,
                                             const EmbeddingDiGraph& data_kg,
                                             const std::unordered_map<std::string, LabelID>& type_name_to_label,
                                             std::vector<LabelID>& query_vertex_labels,
                                             std::vector<LabelID>& data_vertex_labels) {
    std::map<std::vector<LabelID>, LabelID> memo;
    LabelID next_id = 0;

    auto composite_for_key = [&](const std::vector<LabelID>& key) -> LabelID {
        auto it = memo.find(key);
        if (it != memo.end()) {
            return it->second;
        }
        LabelID id = next_id++;
        memo[key] = id;
        return id;
    };

    data_vertex_labels.resize(data_kg.getVerticesCount());
    for (ui v = 0; v < data_kg.getVerticesCount(); ++v) {
        data_vertex_labels[v] = composite_for_key(computeSortedUnifiedTypesForVertex(data_kg, v, type_name_to_label));
    }
    query_vertex_labels.resize(query_kg.getVerticesCount());
    for (ui v = 0; v < query_kg.getVerticesCount(); ++v) {
        query_vertex_labels[v] = composite_for_key(computeSortedUnifiedTypesForVertex(query_kg, v, type_name_to_label));
    }
}


static void buildDataCompositeLabelsAndMemo(const EmbeddingDiGraph& data_kg,
                                            const std::unordered_map<std::string, LabelID>& type_name_to_label,
                                            std::vector<LabelID>& data_vertex_labels,
                                            std::map<std::vector<LabelID>, LabelID>& memo_out,
                                            LabelID& next_id_out) {
    memo_out.clear();
    LabelID next_id = 0;
    auto composite_for_key = [&](const std::vector<LabelID>& key) -> LabelID {
        auto it = memo_out.find(key);
        if (it != memo_out.end()) {
            return it->second;
        }
        LabelID id = next_id++;
        memo_out[key] = id;
        return id;
    };
    data_vertex_labels.resize(data_kg.getVerticesCount());
    for (ui v = 0; v < data_kg.getVerticesCount(); ++v) {
        data_vertex_labels[v] = composite_for_key(computeSortedUnifiedTypesForVertex(data_kg, v, type_name_to_label));
    }
    next_id_out = next_id;
}

static void extendCompositeLabelsForQuery(const EmbeddingDiGraph& query_kg,
                                          const std::unordered_map<std::string, LabelID>& type_name_to_label,
                                          std::map<std::vector<LabelID>, LabelID>& memo,
                                          LabelID& next_id,
                                          std::vector<LabelID>& query_vertex_labels) {
    auto composite_for_key = [&](const std::vector<LabelID>& key) -> LabelID {
        auto it = memo.find(key);
        if (it != memo.end()) {
            return it->second;
        }
        LabelID id = next_id++;
        memo[key] = id;
        return id;
    };
    query_vertex_labels.resize(query_kg.getVerticesCount());
    for (ui v = 0; v < query_kg.getVerticesCount(); ++v) {
        query_vertex_labels[v] = composite_for_key(computeSortedUnifiedTypesForVertex(query_kg, v, type_name_to_label));
    }
}

struct BatchCachedDataGraph {
    Graph* data_graph = nullptr;
    std::unordered_map<std::string, LabelID> unified_relation_base;
    std::unordered_map<std::string, LabelID> unified_type_base;
    std::vector<LabelID> data_vertex_composite_labels;
    std::map<std::vector<LabelID>, LabelID> composite_memo_after_data;
    LabelID composite_next_id_after_data = 0;
    std::vector<std::vector<LabelID>> data_vertex_unified_types;
    std::vector<std::vector<VertexID>> unified_type_to_data_vertices_inv;
};

static void buildUndirectedGraphFromEmbeddingKG(const EmbeddingDiGraph& embedding_graph,
                                                Graph* graph,
                                                const std::unordered_map<std::string, LabelID>& rel_name_to_label,
                                                const std::vector<LabelID>& vertex_composite_labels) {
    const ui vertices_count = embedding_graph.getVerticesCount();
    const ui directed_edges_count = embedding_graph.getEdgesCount();

    if (vertex_composite_labels.size() != static_cast<size_t>(vertices_count)) {
        std::cerr << "vertex_composite_labels size mismatch with |V|." << std::endl;
        exit(-1);
    }

    
    
    std::unordered_map<uint64_t, std::vector<ui>> undirected_edges;
    undirected_edges.reserve(directed_edges_count);

    for (ui edge_id = 0; edge_id < directed_edges_count; ++edge_id) {
        VertexID src = embedding_graph.getEdgeSrc(edge_id);
        VertexID dst = embedding_graph.getEdgeDst(edge_id);
        ui rel_local = embedding_graph.getEdgeRelationId(edge_id);
        const std::string& rel_name = embedding_graph.getRelationName(rel_local);
        auto it = rel_name_to_label.find(rel_name);
        if (it == rel_name_to_label.end()) {
            std::cerr << "Relation name \"" << rel_name << "\" missing from unified relation map." << std::endl;
            exit(-1);
        }
        LabelID unified_rel = it->second;

        if (src == dst) {
            continue;
        }

        VertexID u = src < dst ? src : dst;
        VertexID v = src < dst ? dst : src;
        uint64_t key = (uint64_t)u << 32 | (uint64_t)v;

        undirected_edges[key].push_back(unified_rel);
    }

    std::vector<MatchingEdge> edge_list;
    edge_list.reserve(undirected_edges.size());

    for (auto& kv : undirected_edges) {
        uint64_t key = kv.first;
        VertexID u = (VertexID)(key >> 32);
        VertexID v = (VertexID)(key & 0xffffffffu);
        MatchingEdge item;
        item.source = u;
        item.target = v;
        item.labels = std::move(kv.second);
        edge_list.push_back(std::move(item));
    }

    std::sort(edge_list.begin(), edge_list.end(),
              [](const MatchingEdge& lhs, const MatchingEdge& rhs) {
                  return lhs.source == rhs.source ? lhs.target < rhs.target : lhs.source < rhs.source;
              });

    graph->buildFromMemory(vertex_composite_labels, edge_list);
}

static void prepareBatchCachedDataGraph(EmbeddingDiGraph* data_kg, bool use_vertex_label_intersection,
                                        BatchCachedDataGraph& cache) {
    buildUnifiedRelationNameLabelMapFromDataOnly(*data_kg, cache.unified_relation_base);
    buildUnifiedTypeNameLabelMapFromDataOnly(*data_kg, cache.unified_type_base);
    buildDataCompositeLabelsAndMemo(*data_kg, cache.unified_type_base, cache.data_vertex_composite_labels,
                                    cache.composite_memo_after_data, cache.composite_next_id_after_data);

    cache.data_graph = new Graph(true);
    buildUndirectedGraphFromEmbeddingKG(*data_kg, cache.data_graph, cache.unified_relation_base,
                                        cache.data_vertex_composite_labels);

    if (use_vertex_label_intersection) {
        cache.data_vertex_unified_types.resize(data_kg->getVerticesCount());
        for (ui v = 0; v < data_kg->getVerticesCount(); ++v) {
            cache.data_vertex_unified_types[v] =
                computeSortedUnifiedTypesForVertex(*data_kg, v, cache.unified_type_base);
        }
        LabelID max_unified_type = 0;
        for (const auto& kv : cache.unified_type_base) {
            if (kv.second > max_unified_type) {
                max_unified_type = kv.second;
            }
        }
        buildDataVertexInvertedIndexByUnifiedType(cache.data_vertex_unified_types, max_unified_type,
                                                  cache.unified_type_to_data_vertices_inv);
    }
}

static std::string getPathBaseName(const std::string& path) {
    if (path.empty()) return "result";
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) {
        end -= 1;
    }
    if (end == 0) return "result";
    size_t pos = path.find_last_of("/\\", end - 1);
    if (pos == std::string::npos) return path.substr(0, end);
    return path.substr(pos + 1, end - pos - 1);
}

static bool ensureDirectoryExists(const std::string& dir_path) {
    if (dir_path.empty()) return false;
    if (mkdir(dir_path.c_str(), 0755) == 0) return true;
    return errno == EEXIST;
}


static bool findDirectedRelationByRelationName(const EmbeddingDiGraph* graph, VertexID src, VertexID dst,
                                               const std::string& want_rel_name, ui& relation_id) {
    ui count = 0;
    const VertexID* neighbors = graph->getOutNeighbors(src, count);
    const ui* edge_ids = graph->getOutEdgeIds(src, count);
    for (ui i = 0; i < count; ++i) {
        if (neighbors[i] != dst) {
            continue;
        }
        ui r = graph->getEdgeRelationId(edge_ids[i]);
        if (r >= graph->getRelationsCount()) {
            continue;
        }
        if (graph->getRelationName(r) == want_rel_name) {
            relation_id = r;
            return true;
        }
    }
    return false;
}

static std::string topKResultsOutputDirectory() {
    const char* env = std::getenv("MATCH_RESULTS_DIR");
    if (env != nullptr && env[0] != '\0') {
        return std::string(env);
    }
    return "results";
}

static void dumpTopKResultsToFile(const std::string& query_graph_path,
                                  const Graph* query_graph,
                                  const EmbeddingDiGraph* query_embedding_graph,
                                  const EmbeddingDiGraph* data_embedding_graph,
                                  const std::vector<EvaluateQuery::TopKResult>& topk_results) {
    const std::string out_dir = topKResultsOutputDirectory();
    if (!ensureDirectoryExists(out_dir)) {
        std::cerr << "Failed to create output directory: " << out_dir << std::endl;
        return;
    }

    const std::string file_name = getPathBaseName(query_graph_path) + ".txt";
    const std::string out_path = out_dir + "/" + file_name;

    std::ofstream out(out_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << out_path << std::endl;
        return;
    }

    const ui query_edges = query_embedding_graph->getEdgesCount();

    for (const auto& item : topk_results) {
        out << item.score;

        const bool has_edge_choices = item.edge_sources.size() == query_edges &&
                                      item.edge_targets.size() == query_edges &&
                                      item.edge_relation_ids.size() == query_edges;

        for (ui e = 0; e < query_edges; ++e) {
            VertexID d_src = 0;
            VertexID d_dst = 0;
            ui rel_id = 0;
            bool found = false;

            if (has_edge_choices) {
                d_src = item.edge_sources[e];
                d_dst = item.edge_targets[e];
                rel_id = item.edge_relation_ids[e];
                found = true;
            } else {
                VertexID q_src = query_embedding_graph->getEdgeSrc(e);
                VertexID q_dst = query_embedding_graph->getEdgeDst(e);
                if (q_src >= item.mapping.size() || q_dst >= item.mapping.size()) {
                    continue;
                }

                d_src = item.mapping[q_src];
                d_dst = item.mapping[q_dst];

                ui q_rel = query_embedding_graph->getEdgeRelationId(e);
                const std::string& want_rel_name = query_embedding_graph->getRelationName(q_rel);
                found = findDirectedRelationByRelationName(data_embedding_graph, d_src, d_dst, want_rel_name, rel_id);
                if (!found) {
                    found = findDirectedRelationByRelationName(data_embedding_graph, d_dst, d_src, want_rel_name, rel_id);
                    if (found) {
                        std::swap(d_src, d_dst);
                    }
                }
            }

            std::string rel_name = "UNKNOWN_REL";
            if (found && rel_id < data_embedding_graph->getRelationsCount()) {
                rel_name = data_embedding_graph->getRelationName(rel_id);
            }

            out << "|" << d_src << "---" << rel_id << "---" << d_dst;
            out << "|" << data_embedding_graph->getVertexName(d_src)
                << "---" << rel_name
                << "---" << data_embedding_graph->getVertexName(d_dst);
        }

        out << "\n";
    }

    out.close();
    std::cout << "TopK results saved to: " << out_path << std::endl;
}


static std::vector<std::string> collectBatchQueryDirectories(const std::string& root) {
    std::vector<std::string> dirs;
    DIR* dir = opendir(root.c_str());
    if (dir == nullptr) {
        std::cerr << "Cannot open batch_dir: " << root << std::endl;
        return dirs;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        std::string sub = root;
        if (!sub.empty() && sub.back() != '/') {
            sub += '/';
        }
        sub += ent->d_name;
        struct stat st;
        if (stat(sub.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        std::string graph_file = sub + "/FactKG.graph.txt";
        std::string compact_edges = sub + "/edges.txt";
        std::string compact_stats = sub + "/stats.txt";
        const bool has_pickled = (stat(graph_file.c_str(), &st) == 0 && S_ISREG(st.st_mode));
        const bool has_compact =
            (stat(compact_edges.c_str(), &st) == 0 && S_ISREG(st.st_mode)) &&
            (stat(compact_stats.c_str(), &st) == 0 && S_ISREG(st.st_mode));
        if (has_pickled || has_compact) {
            dirs.push_back(sub);
        }
    }
    closedir(dir);
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

struct MatchingCliConfig {
    std::string input_filter_type;
    std::string input_order_type;
    std::string input_engine_type;
    std::string input_max_embedding_num;
    std::string input_time_limit;
    std::string input_order_num;
    std::string input_distribution_file_path;
    std::string input_topk;
    bool use_vertex_label_intersection = false;
    bool ignore_vertex_labels = true;
};

static void runMatchingFromLoadedEmbeddings(EmbeddingDiGraph* query_embedding_graph,
                                            EmbeddingDiGraph* data_embedding_graph,
                                            const std::string& query_path_for_dump,
                                            const std::string& data_path_for_csv,
                                            long long embedding_disk_load_ns,
                                            const MatchingCliConfig& cfg,
                                            BatchCachedDataGraph* batch_data_cache = nullptr) {
    if (cfg.ignore_vertex_labels) {
        VertexLabelCompat::enableIgnoreVertexLabels();
    }

    auto graph_prep_start = std::chrono::high_resolution_clock::now();

    std::unordered_map<std::string, LabelID> unified_relation_labels;
    std::unordered_map<std::string, LabelID> unified_type_labels;
    std::vector<LabelID> query_vertex_composite_labels;

    const bool reuse_batch_data_graph = (batch_data_cache != nullptr);
    Graph* data_graph = nullptr;

    if (reuse_batch_data_graph) {
        unified_relation_labels = batch_data_cache->unified_relation_base;
        appendQueryOnlyRelationNames(*query_embedding_graph, unified_relation_labels);
        unified_type_labels = batch_data_cache->unified_type_base;
        appendQueryOnlyTypeNames(*query_embedding_graph, unified_type_labels);

        std::map<std::vector<LabelID>, LabelID> composite_memo = batch_data_cache->composite_memo_after_data;
        LabelID next_composite = batch_data_cache->composite_next_id_after_data;
        extendCompositeLabelsForQuery(*query_embedding_graph, unified_type_labels, composite_memo, next_composite,
                                      query_vertex_composite_labels);
        data_graph = batch_data_cache->data_graph;
    } else {
        std::vector<LabelID> data_vertex_composite_labels;
        buildUnifiedRelationNameLabelMapDataFirst(*data_embedding_graph, *query_embedding_graph, unified_relation_labels);
        buildUnifiedTypeNameLabelMapDataFirst(*data_embedding_graph, *query_embedding_graph, unified_type_labels);
        buildCompositeVertexLabelVectors(*query_embedding_graph, *data_embedding_graph, unified_type_labels,
                                         query_vertex_composite_labels, data_vertex_composite_labels);
        data_graph = new Graph(true);
        buildUndirectedGraphFromEmbeddingKG(*data_embedding_graph, data_graph, unified_relation_labels,
                                            data_vertex_composite_labels);
    }

    Graph* query_graph = new Graph(true);
    buildUndirectedGraphFromEmbeddingKG(*query_embedding_graph, query_graph, unified_relation_labels,
                                        query_vertex_composite_labels);
    query_graph->buildCoreTable();

    std::vector<std::vector<LabelID>> query_vertex_unified_types;
    std::vector<std::vector<LabelID>> data_vertex_unified_types;
    std::vector<std::vector<VertexID>> unified_type_to_data_vertices;

    if (cfg.use_vertex_label_intersection) {
        LabelID max_unified_type = 0;
        for (const auto& kv : unified_type_labels) {
            if (kv.second > max_unified_type) {
                max_unified_type = kv.second;
            }
        }

        if (reuse_batch_data_graph) {
            query_vertex_unified_types.resize(query_embedding_graph->getVerticesCount());
            for (ui v = 0; v < query_embedding_graph->getVerticesCount(); ++v) {
                query_vertex_unified_types[v] =
                    computeSortedUnifiedTypesForVertex(*query_embedding_graph, v, unified_type_labels);
            }
            if (batch_data_cache->unified_type_to_data_vertices_inv.size() <= static_cast<size_t>(max_unified_type)) {
                batch_data_cache->unified_type_to_data_vertices_inv.resize(static_cast<size_t>(max_unified_type) + 1);
            }
            VertexLabelCompat::enableIntersectionMode(&query_vertex_unified_types,
                                                      &batch_data_cache->data_vertex_unified_types);
            VertexLabelCompat::setDataInvertedIndex(&batch_data_cache->unified_type_to_data_vertices_inv);
        } else {
            buildPerVertexUnifiedTypeVectors(*query_embedding_graph, *data_embedding_graph, unified_type_labels,
                                             query_vertex_unified_types, data_vertex_unified_types);
            buildDataVertexInvertedIndexByUnifiedType(data_vertex_unified_types, max_unified_type,
                                                      unified_type_to_data_vertices);
            VertexLabelCompat::enableIntersectionMode(&query_vertex_unified_types, &data_vertex_unified_types);
            VertexLabelCompat::setDataInvertedIndex(&unified_type_to_data_vertices);
        }
    }

    auto graph_prep_end = std::chrono::high_resolution_clock::now();
    double graph_prep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(graph_prep_end - graph_prep_start).count();
    double load_graphs_time_in_ns = static_cast<double>(embedding_disk_load_ns) + graph_prep_ns;

    std::cout << "-----" << std::endl;
    std::cout << "Query Graph Meta Information" << std::endl;
    query_graph->printGraphMetaData();
    std::cout << "-----" << std::endl;
    data_graph->printGraphMetaData();

    std::cout << "--------------------------------------------------------------------" << std::endl;

    

    std::cout << "Start queries..." << std::endl;
    std::cout << "-----" << std::endl;
    std::cout << "Filter candidates..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    decltype(start) end;

    ui** candidates = NULL;
    ui* candidates_count = NULL;
    ui* tso_order = NULL;
    TreeNode* tso_tree = NULL;
    ui* cfl_order = NULL;
    TreeNode* cfl_tree = NULL;
    ui* dpiso_order = NULL;
    TreeNode* dpiso_tree = NULL;
    TreeNode* veq_tree = NULL;
    ui* veq_order = NULL;
    TreeNode* ceci_tree = NULL;
    ui* ceci_order = NULL;
    catalog* storage = NULL;
    std::vector<std::unordered_map<VertexID, std::vector<VertexID >>> TE_Candidates;
    std::vector<std::vector<std::unordered_map<VertexID, std::vector<VertexID>>>> NTE_Candidates;
    if (cfg.input_filter_type == "LDF") {
        FilterVertices::LDFFilter(data_graph, query_graph, candidates, candidates_count);
    } else if (cfg.input_filter_type == "NLF") {
        FilterVertices::NLFFilter(data_graph, query_graph, candidates, candidates_count);
    } else if (cfg.input_filter_type == "GQL") {
        FilterVertices::GQLFilter(data_graph, query_graph, candidates, candidates_count);
    } else if (cfg.input_filter_type == "TSO") {
        FilterVertices::TSOFilter(data_graph, query_graph, candidates, candidates_count, tso_order, tso_tree);
    } else if (cfg.input_filter_type == "CFL") {
        FilterVertices::CFLFilter(data_graph, query_graph, candidates, candidates_count, cfl_order, cfl_tree);
    } else if (cfg.input_filter_type == "DPiso") {
        FilterVertices::DPisoFilter(data_graph, query_graph, candidates, candidates_count, dpiso_order, dpiso_tree);
    } else if (cfg.input_filter_type == "VEQ") {
        FilterVertices::VEQFilter(data_graph, query_graph, candidates, candidates_count, veq_order, veq_tree);
    } else if (cfg.input_filter_type == "CECI") {
        FilterVertices::CECIFilter(data_graph, query_graph, candidates, candidates_count, ceci_order, ceci_tree, TE_Candidates, NTE_Candidates);
    } else if (cfg.input_filter_type == "RM") {
        FilterVertices::RMFilter(data_graph, query_graph, candidates, candidates_count, storage);
    } else if (cfg.input_filter_type == "CaLiG") {
        FilterVertices::CaLiGFilter(data_graph, query_graph, candidates, candidates_count);
    }  else {
        std::cout << "The specified filter type '" << cfg.input_filter_type << "' is not supported." << std::endl;
        exit(-1);
    }

    
    ui total_candidates_count = 0;
    for (ui i = 0; i < query_graph->getVerticesCount(); ++i) {
       total_candidates_count += candidates_count[i];
    }

    
    if (cfg.input_filter_type != "CECI")
        FilterVertices::sortCandidates(candidates, candidates_count, query_graph->getVerticesCount());

    end = std::chrono::high_resolution_clock::now();
    double filter_vertices_time_in_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    
#ifdef OPTIMAL_CANDIDATES
    std::vector<ui> optimal_candidates_count;
    double avg_false_positive_ratio = FilterVertices::computeCandidatesFalsePositiveRatio(data_graph, query_graph, candidates,
                                                                                          candidates_count, optimal_candidates_count);
    FilterVertices::printCandidatesInfo(query_graph, candidates_count, optimal_candidates_count);
#endif
    std::cout << "-----" << std::endl;
    std::cout << "Build indices..." << std::endl;

    start = std::chrono::high_resolution_clock::now();

    
    Edges ***edge_matrix = NULL;
    if (cfg.input_filter_type != "CECI") {
        edge_matrix = new Edges **[query_graph->getVerticesCount()];
        for (ui i = 0; i < query_graph->getVerticesCount(); ++i) {
            edge_matrix[i] = new Edges *[query_graph->getVerticesCount()];
        }

        BuildTable::buildTables(data_graph, query_graph, candidates, candidates_count, edge_matrix);
    }

    end = std::chrono::high_resolution_clock::now();
    double build_table_time_in_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    size_t memory_cost_in_bytes = 0;
    if (cfg.input_filter_type != "CECI") {
        memory_cost_in_bytes = BuildTable::computeMemoryCostInBytes(query_graph, candidates_count, edge_matrix);
        
    }
    else {
        memory_cost_in_bytes = BuildTable::computeMemoryCostInBytes(query_graph, candidates_count, ceci_order, ceci_tree,
                TE_Candidates, NTE_Candidates);
        
    }
    std::cout << "-----" << std::endl;
    std::cout << "Generate a matching order..." << std::endl;

    start = std::chrono::high_resolution_clock::now();

    ui* matching_order = NULL;
    ui* pivots = NULL;
    ui** weight_array = NULL;

    size_t order_num = 0;
    sscanf(cfg.input_order_num.c_str(), "%zu", &order_num);

    std::vector<std::vector<ui>> spectrum;
    if (cfg.input_order_type == "QSI") {
        GenerateQueryPlan::generateQSIQueryPlan(data_graph, query_graph, edge_matrix, matching_order, pivots);
    } else if (cfg.input_order_type == "GQL") {
        GenerateQueryPlan::generateGQLQueryPlan(data_graph, query_graph, candidates_count, matching_order, pivots);
    } else if (cfg.input_order_type == "TSO") {
        if (tso_tree == NULL) {
            GenerateFilteringPlan::generateTSOFilterPlan(data_graph, query_graph, tso_tree, tso_order);
        }
        GenerateQueryPlan::generateTSOQueryPlan(query_graph, edge_matrix, matching_order, pivots, tso_tree, tso_order);
    } else if (cfg.input_order_type == "CFL") {
        if (cfl_tree == NULL) {
            int level_count;
            ui* level_offset;
            GenerateFilteringPlan::generateCFLFilterPlan(data_graph, query_graph, cfl_tree, cfl_order, level_count, level_offset);
            delete[] level_offset;
        }
        GenerateQueryPlan::generateCFLQueryPlan(data_graph, query_graph, edge_matrix, matching_order, pivots, cfl_tree, cfl_order, candidates_count);
    } else if (cfg.input_order_type == "DPiso") {
        if (dpiso_tree == NULL) {
            GenerateFilteringPlan::generateDPisoFilterPlan(data_graph, query_graph, dpiso_tree, dpiso_order);
        }

        GenerateQueryPlan::generateDSPisoQueryPlan(query_graph, edge_matrix, matching_order, pivots, dpiso_tree, dpiso_order,
                                                    candidates_count, weight_array);
    } else if (cfg.input_order_type == "CECI") {
        GenerateQueryPlan::generateCECIQueryPlan(query_graph, ceci_tree, ceci_order, matching_order, pivots);
    } else if (cfg.input_order_type == "RI") {
        GenerateQueryPlan::generateRIQueryPlan(data_graph, query_graph, matching_order, pivots);
    } else if (cfg.input_order_type == "VF2PP") {
        GenerateQueryPlan::generateVF2PPQueryPlan(data_graph, query_graph, matching_order, pivots);
    } else if (cfg.input_order_type == "VF3") {
        GenerateQueryPlan::generateVF3QueryPlan(data_graph, query_graph, matching_order, pivots);
    } else if (cfg.input_order_type == "RM") {
        GenerateQueryPlan::generateRMQueryPlan(query_graph, matching_order, edge_matrix, pivots);
    } else if (cfg.input_order_type == "Spectrum") {
        GenerateQueryPlan::generateOrderSpectrum(query_graph, spectrum, order_num);
    } else {
        std::cout << "The specified order type '" << cfg.input_order_type << "' is not supported." << std::endl;
    }

    end = std::chrono::high_resolution_clock::now();
    double generate_query_plan_time_in_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    if (cfg.input_order_type != "Spectrum") {
        GenerateQueryPlan::checkQueryPlanCorrectness(query_graph, matching_order, pivots);
        GenerateQueryPlan::printSimplifiedQueryPlan(query_graph, matching_order);
    }
    else {
        std::cout << "Generate " << spectrum.size() << " matching orders." << std::endl;
    }

    std::cout << "-----" << std::endl;
    std::cout << "Enumerate..." << std::endl;
    size_t output_limit = 0;
    size_t embedding_count = 0;
    if (cfg.input_max_embedding_num == "MAX") {
        output_limit = std::numeric_limits<size_t>::max();
    }
    else {
        sscanf(cfg.input_max_embedding_num.c_str(), "%zu", &output_limit);
    }


#if ENABLE_QFLITER == 1
    EvaluateQuery::qfliter_bsr_graph_ = BuildTable::qfliter_bsr_graph_;
#endif

    size_t topk_limit = 0;
    sscanf(cfg.input_topk.c_str(), "%zu", &topk_limit);
    EvaluateQuery::configureTopKScoring(query_embedding_graph, data_embedding_graph, topk_limit);

    if (topk_limit > 0 && cfg.input_engine_type != "LFTJ") {
        std::cout << "Warning: top-k L2 ranking is currently collected in LFTJ only." << std::endl;
    }
    if (topk_limit > 0 && cfg.input_engine_type == "LFTJ") {
        std::cout << "Note: Global TopK - LFTJ enumerates all embeddings; scoring uses vertex embeddings only "
                    << "(no relation vectors). TopK keeps the best " << topk_limit
                    << " by summed vertex L2 distance." << std::endl;
    }

    size_t call_count = 0;
    size_t time_limit = 0;
    size_t valid_vtx_count = 0;
    sscanf(cfg.input_time_limit.c_str(), "%zu", &time_limit); 

    size_t enum_time_limit_sec = 3600;
    if (const char* env_enum = std::getenv("NUGM_ENUM_TIME_LIMIT_SEC")) {
        if (env_enum[0] != '\0') {
            char* end_ptr = nullptr;
            unsigned long long parsed = std::strtoull(env_enum, &end_ptr, 10);
            if (end_ptr != env_enum && (end_ptr == nullptr || *end_ptr == '\0')) {
                enum_time_limit_sec = static_cast<size_t>(parsed);
            }
        }
    }

    start = std::chrono::high_resolution_clock::now();

    if (cfg.input_engine_type == "EXPLORE") {
        embedding_count = EvaluateQuery::exploreGraph(data_graph, query_graph, edge_matrix, candidates,
                                                      candidates_count, matching_order, pivots, output_limit, call_count);
    } else if (cfg.input_engine_type == "LFTJ") {
        const size_t topk_limit_for_timer = topk_limit;
        
        if (enum_time_limit_sec > 0) {
            EvaluateQuery::setEnumerationTimeLimitFromNowSeconds(enum_time_limit_sec);
        }
        embedding_count = EvaluateQuery::LFTJ(data_graph, query_graph, edge_matrix, candidates, candidates_count,
                                              matching_order, output_limit, call_count, valid_vtx_count);
        const bool enum_timed_out = EvaluateQuery::tookEnumerationTimeLimit();
        EvaluateQuery::clearEnumerationTimeLimit();
        if (enum_timed_out) {
            if (topk_limit_for_timer > 0) {
                std::cout << "Note: LFTJ enumeration time limit (" << enum_time_limit_sec
                          << "s) reached; outputting current TopK (possibly partial)." << std::endl;
            } else {
                std::cout << "Note: LFTJ enumeration time limit (" << enum_time_limit_sec
                          << "s) reached; partial embedding count above." << std::endl;
            }
        }
    } else if (cfg.input_engine_type == "GQL") {
        embedding_count = EvaluateQuery::exploreGraphQLStyle(data_graph, query_graph, candidates, candidates_count,
                                                             matching_order, output_limit, call_count);
    } else if (cfg.input_engine_type == "QSI") {
        embedding_count = EvaluateQuery::exploreQuickSIStyle(data_graph, query_graph, candidates, candidates_count,
                                                             matching_order, pivots, output_limit, call_count);
    } else if (cfg.input_engine_type == "VF3") {
        embedding_count = EvaluateQuery::exploreVF3Style(data_graph, query_graph, candidates, candidates_count,
                                                             matching_order, pivots, output_limit, call_count);
    }
    else if (cfg.input_engine_type == "VEQ") {
        if (veq_tree == NULL) {
            GenerateFilteringPlan::generateDPisoFilterPlan(data_graph, query_graph, veq_tree, veq_order);
        }
        embedding_count = EvaluateQuery::exploreVEQStyle(data_graph, query_graph, veq_tree,
                                                           edge_matrix, candidates, candidates_count,
                                                           output_limit, call_count);
    }
    else if (cfg.input_engine_type == "DPiso") {
        embedding_count = EvaluateQuery::exploreDPisoStyle(data_graph, query_graph, dpiso_tree,
                                                           edge_matrix, candidates, candidates_count,
                                                           weight_array, dpiso_order, output_limit,
                                                           call_count);
    }
    else if (cfg.input_engine_type == "RM") {
        embedding_count = EvaluateQuery::exploreRMStyle(query_graph, data_graph, storage, edge_matrix, candidates, candidates_count,
                                                        matching_order, output_limit, call_count);
    }
    else if (cfg.input_engine_type == "KSS") {
        embedding_count = EvaluateQuery::exploreKSSStyle(query_graph, data_graph, edge_matrix, candidates, candidates_count,
                                                        matching_order, output_limit, call_count);
    }
    else if (cfg.input_engine_type == "Spectrum") {
        spectrum_analysis(data_graph, query_graph, edge_matrix, candidates, candidates_count, output_limit, spectrum, time_limit);
    }
    else if (cfg.input_engine_type == "CECI") {
        embedding_count = EvaluateQuery::exploreCECIStyle(data_graph, query_graph, ceci_tree, candidates, candidates_count, TE_Candidates,
                NTE_Candidates, ceci_order, output_limit, call_count);
    }
    else {
        std::cout << "The specified engine type '" << cfg.input_engine_type << "' is not supported." << std::endl;
        exit(-1);
    }

    end = std::chrono::high_resolution_clock::now();
    double enumeration_time_in_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

#ifdef DISTRIBUTION
    std::ofstream outfile (cfg.input_distribution_file_path , std::ofstream::binary);
    outfile.write((char*)EvaluateQuery::distribution_count_, sizeof(size_t) * data_graph->getVerticesCount());
    delete[] EvaluateQuery::distribution_count_;
#endif

    if (topk_limit > 0) {
        auto topk_results = EvaluateQuery::getTopKResults();
        std::cout << "-----" << std::endl;
        std::cout << "TopK Results (sum of per-vertex L2, lower is better)" << std::endl;
        if (topk_results.empty()) {
            std::cout << "No ranked results collected." << std::endl;
        }
        for (size_t i = 0; i < topk_results.size(); ++i) {
            std::cout << "Rank " << (i + 1) << ", Score=" << std::fixed << std::setprecision(6)
                      << topk_results[i].score << std::endl;
            const auto& mapping = topk_results[i].mapping;
            for (ui u = 0; u < query_graph->getVerticesCount(); ++u) {
                ui v = mapping[u];
                std::cout << "  q" << u << "(" << query_embedding_graph->getVertexName(u) << ")"
                          << " -> d" << v << "(" << data_embedding_graph->getVertexName(v) << ")" << std::endl;
            }
        }
        dumpTopKResultsToFile(query_path_for_dump, query_graph, query_embedding_graph, data_embedding_graph, topk_results);
    }

    std::cout << "--------------------------------------------------------------------" << std::endl;
    std::cout << "Release memories..." << std::endl;

    VertexLabelCompat::reset();
    
    delete[] candidates_count;
    delete[] tso_order;
    delete[] tso_tree;
    delete[] cfl_order;
    delete[] cfl_tree;
    delete[] dpiso_order;
    delete[] dpiso_tree;
    delete[] ceci_order;
    delete[] ceci_tree;
    delete[] matching_order;
    delete[] pivots;
    delete storage;
    for (ui i = 0; i < query_graph->getVerticesCount(); ++i) {
        delete[] candidates[i];
    }
    delete[] candidates;

    if (edge_matrix != NULL) {
        for (ui i = 0; i < query_graph->getVerticesCount(); ++i) {
            for (ui j = 0; j < query_graph->getVerticesCount(); ++j) {
                delete edge_matrix[i][j];
            }
            delete[] edge_matrix[i];
        }
        delete[] edge_matrix;
    }
    if (weight_array != NULL) {
        for (ui i = 0; i < query_graph->getVerticesCount(); ++i) {
            delete[] weight_array[i];
        }
        delete[] weight_array;
    }

    delete query_graph;
    if (!reuse_batch_data_graph) {
        delete data_graph;
    }
    std::cout << "--------------------------------------------------------------------" << std::endl;
    double preprocessing_time_in_ns = filter_vertices_time_in_ns + build_table_time_in_ns + generate_query_plan_time_in_ns;
    double total_time_in_ns = preprocessing_time_in_ns + enumeration_time_in_ns;

    printf("Load graphs time (seconds): %.4lf\n", NANOSECTOSEC(load_graphs_time_in_ns));
    printf("Filter vertices time (seconds): %.4lf\n", NANOSECTOSEC(filter_vertices_time_in_ns));
    printf("Build table time (seconds): %.4lf\n", NANOSECTOSEC(build_table_time_in_ns));
    printf("Generate query plan time (seconds): %.4lf\n", NANOSECTOSEC(generate_query_plan_time_in_ns));
    printf("Enumerate time (seconds): %.4lf\n", NANOSECTOSEC(enumeration_time_in_ns));
    printf("Preprocessing time (seconds): %.4lf\n", NANOSECTOSEC(preprocessing_time_in_ns));
    printf("Total time (seconds): %.4lf\n", NANOSECTOSEC(total_time_in_ns));
    printf("Memory cost (MB): %.4lf\n", BYTESTOMB(memory_cost_in_bytes));
    printf("#Embeddings: %zu\n", embedding_count);
    printf("Call Count: %zu\n", call_count);
    printf("Per Call Count Time (nanoseconds): %.4lf\n", enumeration_time_in_ns / (call_count == 0 ? 1 : call_count));
    std::cout << "End." << std::endl;

    
    std::fstream output;
    output.open("results/metrics.csv", std::ios::out | std::ios::app);
    
    output << query_path_for_dump << ",";
    output << data_path_for_csv << ",";
    output << cfg.input_filter_type << ",";
    output << cfg.input_order_type << ",";
    output << cfg.input_engine_type << ",";
    output << NANOSECTOSEC(load_graphs_time_in_ns) << ",";
    output << NANOSECTOSEC(filter_vertices_time_in_ns) << ",";
    output << NANOSECTOSEC(build_table_time_in_ns) << ",";
    output << NANOSECTOSEC(generate_query_plan_time_in_ns) << ",";
    output << NANOSECTOSEC(enumeration_time_in_ns) << ",";
    output << NANOSECTOSEC(preprocessing_time_in_ns) << ",";
    output << NANOSECTOSEC(total_time_in_ns) << ",";
    output << embedding_count << ",";
    output << call_count << std::endl;
    
    output.close();
}
int main(int argc, char** argv) {
    MatchingCommand command(argc, argv);
    std::string batch_dir = command.getBatchQueryDir();
    std::string input_query_graph_file = command.getQueryGraphFilePath();
    std::string input_data_graph_file = command.getDataGraphFilePath();
    std::string input_csr_file_path = command.getCSRFilePath();

    std::string vertex_label_match_opt = command.getVertexLabelMatch();
    std::string vl_lower = vertex_label_match_opt;
    for (char& ch : vl_lower) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    MatchingCliConfig cfg;
    cfg.input_filter_type = command.getFilterType();
    cfg.input_order_type = command.getOrderType();
    cfg.input_engine_type = command.getEngineType();
    cfg.input_max_embedding_num = command.getMaximumEmbeddingNum();
    cfg.input_time_limit = command.getTimeLimit();
    cfg.input_order_num = command.getOrderNum();
    cfg.input_distribution_file_path = command.getDistributionFilePath();
    cfg.input_topk = command.getTopK();
    cfg.ignore_vertex_labels = (vl_lower == "none" || vl_lower == "ignore");
    cfg.use_vertex_label_intersection = (vl_lower == "intersection");

    std::cout << "Command Line:" << std::endl;
    std::cout << "\tData Graph CSR (unused): " << input_csr_file_path << std::endl;
    std::cout << "\tData Graph (new_graph folder): " << input_data_graph_file << std::endl;
    if (!batch_dir.empty()) {
        std::cout << "\tBatch queries (-batch_dir): " << batch_dir << std::endl;
        std::cout << "\tQuery Graph (-q, unused in batch mode): " << input_query_graph_file << std::endl;
    } else {
        std::cout << "\tQuery Graph (new_graph folder): " << input_query_graph_file << std::endl;
    }
    std::cout << "\tFilter Type: " << cfg.input_filter_type << std::endl;
    std::cout << "\tOrder Type: " << cfg.input_order_type << std::endl;
    std::cout << "\tEngine Type: " << cfg.input_engine_type << std::endl;
    std::cout << "\tOutput Limit: " << cfg.input_max_embedding_num << std::endl;
    std::cout << "\tTime Limit (seconds): " << cfg.input_time_limit << std::endl;
    std::cout << "\tOrder Num: " << cfg.input_order_num << std::endl;
    std::cout << "\tDistribution File Path: " << cfg.input_distribution_file_path << std::endl;
    std::cout << "\tTopK (sum of per-vertex L2, undirected match + unified labels): " << cfg.input_topk << std::endl;
    std::cout << "\tVertex label match (-vlmatch): " << vertex_label_match_opt
              << (cfg.ignore_vertex_labels ? " (no vertex label filter)"
                  : (cfg.use_vertex_label_intersection ? " (type intersection)" : " (composite equality)"))
              << std::endl;
    std::cout << "\tEdge label match: any nonempty overlap between query and data edge label sets"
              << std::endl;
    if (batch_dir.empty()) {
        std::cout << "\tTip: use -batch_dir <query directory> to reuse the data graphs across a query batch."
                  << std::endl;
    }
    std::cout << "--------------------------------------------------------------------" << std::endl;

    if (!batch_dir.empty()) {
        std::vector<std::string> query_paths = collectBatchQueryDirectories(batch_dir);
        if (query_paths.empty()) {
            std::cerr << "batch_dir has no subdirectories containing FactKG.graph.txt or edges.txt+stats.txt: "
                      << batch_dir << std::endl;
            return 1;
        }

        std::cout << "Batch mode: loading data KG once from disk (" << query_paths.size() << " queries)..." << std::endl;
        auto data_t0 = std::chrono::high_resolution_clock::now();
        EmbeddingDiGraph* data_embedding_graph = new EmbeddingDiGraph(false, false);
        data_embedding_graph->loadFromKG(input_data_graph_file, cfg.ignore_vertex_labels);
        long long data_disk_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::high_resolution_clock::now() - data_t0)
                                     .count();
        printf("Data KG disk load (seconds): %.4lf\n", NANOSECTOSEC(static_cast<double>(data_disk_ns)));

        BatchCachedDataGraph batch_cache;
        auto cache_t0 = std::chrono::high_resolution_clock::now();
        prepareBatchCachedDataGraph(data_embedding_graph, cfg.use_vertex_label_intersection, batch_cache);
        long long cache_build_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::high_resolution_clock::now() - cache_t0)
                                       .count();
        printf("Batch: built in-memory data Graph once (seconds): %.4lf\n",
               NANOSECTOSEC(static_cast<double>(cache_build_ns)));

        for (size_t qi = 0; qi < query_paths.size(); ++qi) {
            std::cout << "\n===== Batch " << (qi + 1) << "/" << query_paths.size() << " : " << query_paths[qi]
                      << " =====" << std::endl;
            EvaluateQuery::clearTopKResults();
            auto q_t0 = std::chrono::high_resolution_clock::now();
            EmbeddingDiGraph* query_embedding_graph = new EmbeddingDiGraph(false, false);
            query_embedding_graph->loadFromKG(query_paths[qi], cfg.ignore_vertex_labels);
            long long query_disk_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::high_resolution_clock::now() - q_t0)
                                          .count();
            printf("Query KG disk load (seconds): %.4lf\n", NANOSECTOSEC(static_cast<double>(query_disk_ns)));

            runMatchingFromLoadedEmbeddings(query_embedding_graph, data_embedding_graph, query_paths[qi],
                                            input_data_graph_file, query_disk_ns, cfg, &batch_cache);

            delete query_embedding_graph;
        }

        delete batch_cache.data_graph;
        delete data_embedding_graph;
        VertexLabelCompat::reset();
        std::cout << "\nBatch finished." << std::endl;
        return 0;
    }

    std::cout << "Load graphs..." << std::endl;

    auto embed_t0 = std::chrono::high_resolution_clock::now();

    EmbeddingDiGraph* query_embedding_graph = new EmbeddingDiGraph(false, false);
    query_embedding_graph->loadFromKG(input_query_graph_file, cfg.ignore_vertex_labels);
    EmbeddingDiGraph* data_embedding_graph = new EmbeddingDiGraph(false, false);
    data_embedding_graph->loadFromKG(input_data_graph_file, cfg.ignore_vertex_labels);


    long long embed_disk_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::high_resolution_clock::now() - embed_t0)
                                  .count();

    runMatchingFromLoadedEmbeddings(query_embedding_graph, data_embedding_graph, input_query_graph_file,
                                    input_data_graph_file, embed_disk_ns, cfg);
    delete query_embedding_graph;
    delete data_embedding_graph;
    VertexLabelCompat::reset();
    return 0;
}
