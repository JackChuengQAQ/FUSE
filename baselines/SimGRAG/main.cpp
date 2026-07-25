#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <filesystem>
#else
#include <dirent.h>
#endif
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "naive_greedy.h"
#include "query_vocab.h"
#include "graph/new_graph.h"

namespace {
struct MaterializedQuery {
    QueryPattern pattern;
    std::vector<QueryDirectedEdge> edges_in_query_order;
    std::vector<const float*> node_embeddings;
    std::vector<VertexID> old_to_new;
};

inline bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

inline std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + file;
    return dir + "/" + file;
}

struct StopWatch {
    std::chrono::high_resolution_clock::time_point t0;
    StopWatch() { reset(); }
    void reset() { t0 = std::chrono::high_resolution_clock::now(); }
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0).count();
    }
};

inline double readProcStatusKb(const char* key) {
#ifdef _WIN32
    (void)key;
    return 0.0;
#else
    std::ifstream f("/proc/self/status");
    if (!f.is_open()) return 0.0;
    std::string line;
    size_t klen = std::strlen(key);
    while (std::getline(f, line)) {
        if (line.compare(0, klen, key) == 0) {
            const char* p = line.c_str() + klen;
            while (*p == ':' || *p == ' ' || *p == '\t') ++p;
            return std::atof(p);
        }
    }
    return 0.0;
#endif
}

inline double readVmRssMb() { return readProcStatusKb("VmRSS") / 1024.0; }
inline double readVmPeakMb() { return readProcStatusKb("VmHWM") / 1024.0; }

struct QueryTiming {
    std::string id;
    double query_load_ms = 0.0;
    double materialize_ms = 0.0;
    double vocab_map_ms = 0.0;
    double retrieve_ms = 0.0;
    double write_ms = 0.0;
    double total_ms = 0.0;
    size_t results_count = 0;
    double best_score = 0.0;
    double worst_score = 0.0;
    size_t matched_edges_count = 0;
    double rss_after_query_mb = 0.0;
    bool timed_out = false;
};

struct ComboTiming {
    std::string kg_path;
    std::string queries_root;
    std::string baseline_root;
    std::string mode_name;
    ui K = 0;
    ui node_sim_topk = 0;
    bool undirected = false;
    int start_id = -1;
    int end_id = -1;
    ui query_time_limit_sec = 3600;

    double kg_load_ms = 0.0;
    double ann_build_ms = 0.0;
    std::string ann_index_dir;
    VertexID ann_entry_point = 0;
    double kg_vocab_table_ms = 0.0;
    double queries_scan_ms = 0.0;
    size_t queries_processed = 0;
    double combo_queries_ms = 0.0;  
    double total_ms = 0.0;          

    ui kg_vertices = 0;
    ui kg_relations = 0;
    ui kg_labels = 0;
    long long kg_edges = 0;
    double mem_rss_after_kg_mb = 0.0;
    double mem_rss_peak_mb = 0.0;

    std::vector<QueryTiming> queries;
};

inline std::string formatFixed(double v, int prec) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

void writeTimingJson(const std::string& path, const ComboTiming& t) {
    std::ofstream f(path.c_str());
    if (!f.is_open()) return;
    f << "{\n";
    f << "  \"run_meta\": {\n";
    f << "    \"kg_path\": \"" << t.kg_path << "\",\n";
    f << "    \"queries_root\": \"" << t.queries_root << "\",\n";
    f << "    \"baseline_root\": \"" << t.baseline_root << "\",\n";
    f << "    \"mode\": \"" << t.mode_name << "\",\n";
    f << "    \"top_k\": " << t.K << ",\n";
    f << "    \"node_sim_topk\": " << t.node_sim_topk << ",\n";
    f << "    \"undirected\": " << (t.undirected ? "true" : "false") << ",\n";
    f << "    \"graph_format\": \"new_graph\",\n";
    f << "    \"start_id\": " << t.start_id << ",\n";
    f << "    \"end_id\": " << t.end_id << ",\n";
    f << "    \"query_time_limit_sec\": " << t.query_time_limit_sec << "\n";
    f << "  },\n";
    f << "  \"global\": {\n";
    f << "    \"kg_load_ms\": " << formatFixed(t.kg_load_ms, 3) << ",\n";
    f << "    \"ann_build_ms\": " << formatFixed(t.ann_build_ms, 3) << ",\n";
    f << "    \"ann_index_dir\": \"" << t.ann_index_dir << "\",\n";
    f << "    \"ann_entry_point\": " << t.ann_entry_point << ",\n";
    f << "    \"kg_vocab_table_ms\": " << formatFixed(t.kg_vocab_table_ms, 3) << ",\n";
    f << "    \"queries_scan_ms\": " << formatFixed(t.queries_scan_ms, 3) << ",\n";
    f << "    \"queries_processed\": " << t.queries_processed << ",\n";
    f << "    \"combo_queries_ms\": " << formatFixed(t.combo_queries_ms, 3) << ",\n";
    f << "    \"total_ms\": " << formatFixed(t.total_ms, 3) << ",\n";
    f << "    \"kg_vertices\": " << t.kg_vertices << ",\n";
    f << "    \"kg_edges\": " << t.kg_edges << ",\n";
    f << "    \"kg_relations\": " << t.kg_relations << ",\n";
    f << "    \"kg_labels\": " << t.kg_labels << ",\n";
    f << "    \"mem_rss_after_kg_mb\": " << formatFixed(t.mem_rss_after_kg_mb, 2) << ",\n";
    f << "    \"mem_rss_peak_mb\": " << formatFixed(t.mem_rss_peak_mb, 2) << "\n";
    f << "  },\n";
    f << "  \"queries\": {\n";
    for (size_t i = 0; i < t.queries.size(); ++i) {
        const QueryTiming& q = t.queries[i];
        f << "    \"" << q.id << "\": {\n";
        f << "      \"query_load_ms\": " << formatFixed(q.query_load_ms, 3) << ",\n";
        f << "      \"materialize_ms\": " << formatFixed(q.materialize_ms, 3) << ",\n";
        f << "      \"vocab_map_ms\": " << formatFixed(q.vocab_map_ms, 3) << ",\n";
        f << "      \"retrieve_ms\": " << formatFixed(q.retrieve_ms, 3) << ",\n";
        f << "      \"write_ms\": " << formatFixed(q.write_ms, 3) << ",\n";
        f << "      \"total_ms\": " << formatFixed(q.total_ms, 3) << ",\n";
        f << "      \"timed_out\": " << (q.timed_out ? "true" : "false") << ",\n";
        f << "      \"results\": { \"count\": " << q.results_count
          << ", \"best_score\": " << formatFixed(q.best_score, 6)
          << ", \"worst_score\": " << formatFixed(q.worst_score, 6)
          << ", \"matched_edges_total\": " << q.matched_edges_count << " },\n";
        f << "      \"memory\": { \"rss_after_query_mb\": " << formatFixed(q.rss_after_query_mb, 2) << " }\n";
        f << "    }" << (i + 1 < t.queries.size() ? "," : "") << "\n";
    }
    f << "  }\n";
    f << "}\n";
    f.close();
}

inline void ensureCleanDir(const std::string& path) {
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(path), ec);
    ec.clear();
    std::filesystem::create_directories(std::filesystem::path(path), ec);
#else
    std::string rm_cmd = "rm -rf \"" + path + "\"";
    std::string mk_cmd = "mkdir -p \"" + path + "\"";
    int rc = std::system(rm_cmd.c_str());
    (void)rc;
    rc = std::system(mk_cmd.c_str());
    (void)rc;
#endif
}

std::vector<ui> parseUiList(const std::string& s) {
    std::vector<ui> out;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        
        size_t l = 0;
        while (l < token.size() && (token[l] == ' ' || token[l] == '\t')) l++;
        size_t r = token.size();
        while (r > l && (token[r - 1] == ' ' || token[r - 1] == '\t')) r--;
        if (l >= r) continue;
        ui v = static_cast<ui>(std::stoul(token.substr(l, r - l)));
        out.push_back(v);
    }
    return out;
}

std::vector<std::string> parseStringList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        size_t l = 0;
        while (l < token.size() && (token[l] == ' ' || token[l] == '\t')) l++;
        size_t r = token.size();
        while (r > l && (token[r - 1] == ' ' || token[r - 1] == '\t')) r--;
        if (l >= r) continue;
        std::string t = token.substr(l, r - l);
        for (size_t i = 0; i < t.size(); ++i) {
            t[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
        }
        out.push_back(t);
    }
    return out;
}

std::vector<std::pair<int, std::string> > collectQueryEntries(const std::string& queries_root) {
    std::vector<std::pair<int, std::string> > entries;
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::directory_iterator it(std::filesystem::path(queries_root), ec);
    if (ec) {
        return entries;
    }
    for (const std::filesystem::directory_entry& entry : it) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        std::string name = entry.path().filename().string();
        if (!isAllDigits(name)) continue;
        entries.push_back(std::make_pair(std::atoi(name.c_str()), name));
    }
#else
    DIR* dir = opendir(queries_root.c_str());
    if (dir == NULL) {
        return entries;
    }
    struct dirent* ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        std::string name(ent->d_name);
        if (!isAllDigits(name)) continue;
        entries.push_back(std::make_pair(std::atoi(name.c_str()), name));
    }
    closedir(dir);
#endif
    std::sort(entries.begin(), entries.end(),
              [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second < b.second;
              });
    return entries;
}

MaterializedQuery buildMaterializedQuery(const EmbeddingDiGraph& query_graph, const std::string& query_path) {
    const ui original_qv = query_graph.getVerticesCount();
    std::unordered_map<std::string, VertexID> name_to_new_id;
    std::vector<VertexID> old_to_new(original_qv, std::numeric_limits<VertexID>::max());
    std::vector<VertexID> new_to_rep_old;

    for (VertexID old_u = 0; old_u < original_qv; ++old_u) {
        const std::string& name = query_graph.getVertexName(old_u);
        std::unordered_map<std::string, VertexID>::const_iterator it = name_to_new_id.find(name);
        if (it == name_to_new_id.end()) {
            VertexID new_u = static_cast<VertexID>(new_to_rep_old.size());
            name_to_new_id[name] = new_u;
            old_to_new[old_u] = new_u;
            new_to_rep_old.push_back(old_u);
        } else {
            old_to_new[old_u] = it->second;
        }
    }

    std::vector<QueryDirectedEdge> edges;
    std::ifstream edge_file(joinPath(query_path, "edges.txt").c_str());
    if (edge_file.is_open()) {
        while (true) {
            VertexID src = 0;
            VertexID dst = 0;
            ui rel = 0;
            if (!(edge_file >> src >> rel >> dst)) break;
            if (src >= original_qv || dst >= original_qv) continue;
            edges.push_back(QueryDirectedEdge{old_to_new[src], old_to_new[dst], rel});
        }
        edge_file.close();
    }
    if (edges.empty()) {
        for (VertexID old_u = 0; old_u < original_qv; ++old_u) {
            ui out_count = 0;
            const VertexID* out_nbs = query_graph.getOutNeighbors(old_u, out_count);
            const ui* out_eids = query_graph.getOutEdgeIds(old_u, out_count);
            for (ui i = 0; i < out_count; ++i) {
                ui rel = query_graph.getEdgeRelationId(out_eids[i]);
                edges.push_back(QueryDirectedEdge{old_to_new[old_u], old_to_new[out_nbs[i]], rel});
            }
        }
    }

    MaterializedQuery out;
    out.pattern.num_vertices = static_cast<ui>(new_to_rep_old.size());
    out.pattern.out_neighbors.assign(out.pattern.num_vertices, std::vector<QueryNeighborInfo>());
    out.pattern.in_neighbors.assign(out.pattern.num_vertices, std::vector<QueryNeighborInfo>());

    for (size_t i = 0; i < edges.size(); ++i) {
        const QueryDirectedEdge& e = edges[i];
        if (e.src >= out.pattern.num_vertices || e.dst >= out.pattern.num_vertices || e.src == e.dst) {
            continue;
        }
        out.pattern.out_neighbors[e.src].push_back(QueryNeighborInfo{e.dst, e.relation_id});
        out.pattern.in_neighbors[e.dst].push_back(QueryNeighborInfo{e.src, e.relation_id});
    }

    out.node_embeddings.resize(out.pattern.num_vertices, NULL);
    for (ui u = 0; u < out.pattern.num_vertices; ++u) {
        out.node_embeddings[u] = query_graph.getVertexEmbedding(new_to_rep_old[u]);
    }

    out.edges_in_query_order.swap(edges);
    out.old_to_new = std::move(old_to_new);
    return out;
}

void writeResults(const std::string& out_file,
                  const EmbeddingDiGraph& kg,
                  const std::vector<NaiveGreedyMatcher::MatchResult>& results) {
    std::ofstream fout(out_file.c_str());
    fout << std::setprecision(8);
    for (size_t i = 0; i < results.size(); ++i) {
        fout << results[i].score;
        for (size_t e = 0; e < results[i].matched_edges.size(); ++e) {
            VertexID src = std::get<0>(results[i].matched_edges[e]);
            ui rel = std::get<1>(results[i].matched_edges[e]);
            VertexID dst = std::get<2>(results[i].matched_edges[e]);
            fout << "|" << src << "---" << rel << "---" << dst;
            fout << "|" << kg.getVertexName(src) << "---" << kg.getRelationName(rel)
                 << "---" << kg.getVertexName(dst);
        }
        fout << "\n";
    }
    fout.close();
}
}  

int main(int argc, char** argv) {
    std::string kg_path = "data/graph";
    std::string queries_root = "data/queries";
    std::string baseline_root = "results";
    bool undirected_mode = true;
    std::vector<ui> k_list = {3};
    std::vector<ui> node_sim_topk_list = {128, 256, 512, 1024, 2048};
    int start_id = -1;
    int end_id = -1;
    ui query_time_limit_sec = 3600;

    if (argc > 1) kg_path = argv[1];
    if (argc > 2) queries_root = argv[2];
    if (argc > 3) baseline_root = argv[3];
    if (argc > 4) undirected_mode = (std::stoi(argv[4]) != 0);
    if (argc > 5) {
        std::vector<ui> parsed = parseUiList(argv[5]);
        if (!parsed.empty()) k_list = parsed;
    }
    if (argc > 6) {
        std::vector<ui> parsed = parseUiList(argv[6]);
        if (!parsed.empty()) node_sim_topk_list = parsed;
    }
    if (argc > 8) start_id = std::stoi(argv[8]);
    if (argc > 9) end_id = std::stoi(argv[9]);
    if (argc > 10) query_time_limit_sec = static_cast<ui>(std::stoul(argv[10]));

    StopWatch sw_scan;
    std::vector<std::pair<int, std::string> > query_entries = collectQueryEntries(queries_root);
    double queries_scan_ms = sw_scan.elapsed_ms();
    if (query_entries.empty()) {
        std::cerr << "No numeric query directories found in: " << queries_root << std::endl;
        return 1;
    }

    const auto begin_all = std::chrono::high_resolution_clock::now();
    StopWatch sw_kg;
    EmbeddingDiGraph kg(true, true);
    kg.loadFromKG(kg_path, true);
    double kg_load_ms = sw_kg.elapsed_ms();
    double mem_rss_after_kg_mb = readVmRssMb();
    StopWatch sw_vocab;
    std::unordered_map<std::string, ui> kg_rel_name_to_id;
    std::unordered_map<std::string, LabelID> kg_label_name_to_id;
    buildKgNameTables(kg, kg_rel_name_to_id, kg_label_name_to_id);
    double kg_vocab_table_ms = sw_vocab.elapsed_ms();

    StopWatch sw_ann;
    VamanaANN ann(false);
    VamanaANN::BuildConfig ann_cfg;
    ann_cfg.use_full_vamana_link = true;
    ann_cfg.max_degree = 32;
    ann_cfg.Lbuild = 128;
    ann_cfg.max_candidate_size = 750;
    ann_cfg.num_build_threads = 128;
    const std::string ann_index_dir = joinPath(kg_path, "index/node_vamana");
    ann.buildOrLoad(kg, ann_cfg, ann_index_dir);
    const VertexID ann_entry_point = ann.getEntryPoint();
    double ann_build_ms = sw_ann.elapsed_ms();
    std::cout << "[baseline] ann_index_dir=" << ann_index_dir
              << " entry_point=" << ann_entry_point
              << " ann_build_ms=" << ann_build_ms << std::endl;

    struct ModeItem {
        NaiveGreedyMatcher::Mode mode;
        std::string name;
    };
    std::vector<ModeItem> modes = {
        {NaiveGreedyMatcher::Mode::Greedy, "greedy"},
        {NaiveGreedyMatcher::Mode::Naive, "naive"}
    };
    if (argc > 7) {
        std::vector<std::string> mode_names = parseStringList(argv[7]);
        std::vector<ModeItem> filtered_modes;
        for (size_t i = 0; i < mode_names.size(); ++i) {
            if (mode_names[i] == "naive") {
                filtered_modes.push_back(ModeItem{NaiveGreedyMatcher::Mode::Naive, "naive"});
            } else if (mode_names[i] == "greedy") {
                filtered_modes.push_back(ModeItem{NaiveGreedyMatcher::Mode::Greedy, "greedy"});
            }
        }
        if (!filtered_modes.empty()) {
            modes.swap(filtered_modes);
        } else {
            std::cerr << "[WARN] mode_list has no valid mode. Use: naive,greedy" << std::endl;
            std::cerr << "       fallback to default modes: greedy,naive" << std::endl;
        }
    }

    for (size_t m = 0; m < modes.size(); ++m) {
        for (size_t ki = 0; ki < k_list.size(); ++ki) {
            ui K = k_list[ki];
            for (size_t p = 0; p < node_sim_topk_list.size(); ++p) {
                ui node_sim_topk = node_sim_topk_list[p];
                std::string combo_dir_name = "K" + std::to_string(K) + "_nodetopk" +
                                             std::to_string(node_sim_topk);
                std::string mode_root = joinPath(baseline_root, modes[m].name);
                std::string tmp_dir = joinPath(mode_root, combo_dir_name + "_tmp_running");
                ensureCleanDir(tmp_dir);

                ComboTiming combo_timing;
                combo_timing.kg_path = kg_path;
                combo_timing.queries_root = queries_root;
                combo_timing.baseline_root = baseline_root;
                combo_timing.mode_name = modes[m].name;
                combo_timing.K = K;
                combo_timing.node_sim_topk = node_sim_topk;
                combo_timing.undirected = undirected_mode;
                combo_timing.start_id = start_id;
                combo_timing.end_id = end_id;
                combo_timing.query_time_limit_sec = query_time_limit_sec;
                combo_timing.kg_load_ms = kg_load_ms;
                combo_timing.ann_build_ms = ann_build_ms;
                combo_timing.ann_index_dir = ann_index_dir;
                combo_timing.ann_entry_point = ann_entry_point;
                combo_timing.kg_vocab_table_ms = kg_vocab_table_ms;
                combo_timing.queries_scan_ms = queries_scan_ms;
                combo_timing.kg_vertices = kg.getVerticesCount();
                combo_timing.kg_edges = static_cast<long long>(kg.getEdgesCount());
                combo_timing.kg_relations = kg.getRelationsCount();
                combo_timing.kg_labels = kg.getLabelsCount();
                combo_timing.mem_rss_after_kg_mb = mem_rss_after_kg_mb;
                combo_timing.queries.reserve(query_entries.size());

                StopWatch sw_combo;

                NaiveGreedyMatcher matcher(kg, false);
                matcher.setUndirectedMode(undirected_mode);
                matcher.setQueryTimeLimitSec(query_time_limit_sec);

                std::cout << "[Run] mode=" << modes[m].name
                          << " K=" << K
                          << " node_sim_topk=" << node_sim_topk
                          << " graph_format=new_graph"
                          << " time_limit_sec=" << query_time_limit_sec
                          << " queries=" << query_entries.size() << std::endl;

                for (size_t i = 0; i < query_entries.size(); ++i) {
                    const int qid = query_entries[i].first;
                    if (start_id >= 0 && qid < start_id) {
                        continue;
                    }
                    if (end_id >= 0 && qid > end_id) {
                        continue;
                    }
                    const std::string& qname = query_entries[i].second;
                    std::string query_path = joinPath(queries_root, qname);

                    QueryTiming qt;
                    qt.id = qname;
                    StopWatch sw_q_total;

                    StopWatch sw_step;
                    EmbeddingDiGraph query_graph(false, true);
                    query_graph.loadFromKG(query_path, true);
                    qt.query_load_ms = sw_step.elapsed_ms();

                    std::string out_file = joinPath(tmp_dir, qname + ".txt");
                    if (query_graph.getVerticesCount() == 0) {
                        std::ofstream empty_out(out_file.c_str());
                        empty_out.close();
                        qt.total_ms = sw_q_total.elapsed_ms();
                        qt.rss_after_query_mb = readVmRssMb();
                        combo_timing.queries.push_back(qt);
                        continue;
                    }

                    sw_step.reset();
                    MaterializedQuery mq = buildMaterializedQuery(query_graph, query_path);
                    qt.materialize_ms = sw_step.elapsed_ms();

                    sw_step.reset();
                    QueryVocabMap vocab = buildQueryVocabMap(
                        query_graph,
                        mq.old_to_new,
                        mq.pattern.num_vertices,
                        kg_rel_name_to_id,
                        kg_label_name_to_id);
                    matcher.setRelationNameMap(vocab.rel_q2k);
                    (void)vocab;
                    qt.vocab_map_ms = sw_step.elapsed_ms();

                    sw_step.reset();
                    const VamanaANN* ann_ptr = &ann;
                    std::vector<NaiveGreedyMatcher::MatchResult> results =
                        matcher.retrieve(mq.pattern,
                                         mq.node_embeddings,
                                         K,
                                         node_sim_topk,
                                         ann_ptr,
                                         mq.edges_in_query_order,
                                         modes[m].mode);
                    qt.retrieve_ms = sw_step.elapsed_ms();
                    qt.timed_out = matcher.timedOut();

                    sw_step.reset();
                    writeResults(out_file, kg, results);
                    qt.write_ms = sw_step.elapsed_ms();

                    qt.results_count = results.size();
                    if (!results.empty()) {
                        double best = results[0].score;
                        double worst = results[0].score;
                        size_t edges_sum = 0;
                        for (size_t r = 0; r < results.size(); ++r) {
                            if (results[r].score < best) best = results[r].score;
                            if (results[r].score > worst) worst = results[r].score;
                            edges_sum += results[r].matched_edges.size();
                        }
                        qt.best_score = best;
                        qt.worst_score = worst;
                        qt.matched_edges_count = edges_sum;
                    }
                    qt.total_ms = sw_q_total.elapsed_ms();
                    qt.rss_after_query_mb = readVmRssMb();
                    combo_timing.queries.push_back(qt);
                }

                combo_timing.combo_queries_ms = sw_combo.elapsed_ms();
                combo_timing.queries_processed = combo_timing.queries.size();
                combo_timing.mem_rss_peak_mb = readVmPeakMb();
                
                
                
                combo_timing.total_ms = combo_timing.kg_load_ms
                                      + combo_timing.ann_build_ms
                                      + combo_timing.kg_vocab_table_ms
                                      + combo_timing.queries_scan_ms
                                      + combo_timing.combo_queries_ms;

                const long long combo_ms = static_cast<long long>(combo_timing.total_ms);
                std::string final_dir = joinPath(mode_root, combo_dir_name + "_" + std::to_string(combo_ms) + "ms");
                writeTimingJson(joinPath(tmp_dir, "timing.json"), combo_timing);

#ifdef _WIN32
                std::error_code fs_ec;
                std::filesystem::remove_all(std::filesystem::path(final_dir), fs_ec);
                fs_ec.clear();
                std::filesystem::rename(std::filesystem::path(tmp_dir), std::filesystem::path(final_dir), fs_ec);
                if (fs_ec) {
                    std::cerr << "[ERROR] rename " << tmp_dir << " -> " << final_dir
                              << ": " << fs_ec.message() << std::endl;
                }
#else
                std::string rm_cmd = "rm -rf \"" + final_dir + "\"";
                std::string mv_cmd = "mv \"" + tmp_dir + "\" \"" + final_dir + "\"";
                int rc = std::system(rm_cmd.c_str());
                (void)rc;
                rc = std::system(mv_cmd.c_str());
                (void)rc;
#endif
                std::cout << "  -> " << final_dir << std::endl;
            }
        }
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - begin_all).count();
    std::cout << "Done in " << elapsed_ms << " ms" << std::endl;
    return 0;
}
