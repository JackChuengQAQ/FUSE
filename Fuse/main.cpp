#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "new_graph.h"
#include "cover_refinement.h"
#include "global_search.h"
#include "metrics_util.h"
#include "query_vocab.h"
#include "upper_bound_pruning.h"

namespace {
struct MaterializedQuery {
    QueryPattern pattern;
    std::vector<QueryDirectedEdge> edges_in_query_order;
    std::vector<const float*> node_embeddings;
    std::vector<VertexID> old_to_new;
    ui new_count;
};








class Stopwatch {
public:
    Stopwatch() { reset(); }
    void reset() { t0_ = std::chrono::high_resolution_clock::now(); }
    double elapsed_ms() const {
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0_).count();
        return static_cast<double>(us) / 1000.0;
    }
private:
    std::chrono::high_resolution_clock::time_point t0_;
};

using TimingKV = std::vector<std::pair<std::string, double> >;




using JsonKV = std::vector<std::pair<std::string, std::string> >;



struct QueryRichRecord {
    std::string qname;
    int qid = -1;
    TimingKV steps;          

    
    ui raw_vertices = 0, dedup_vertices = 0, edges = 0;
    ui cover_size = 0, independent_size = 0, max_degree = 0;
    int root = -1;           

    
    ui q_relations = 0, q_relations_unmapped = 0;
    ui q_labels = 0, q_labels_unmapped = 0, q_nodes_no_label_intersection = 0;

    
    CandStats cands_after_init, cands_after_refine, cands_after_prune;

    
    uint64_t ub_cells_after_compute = 0, ub_cells_after_prune = 0;

    
    CandInitStats cand_init_stats;
    ui refine_iters_actual = 0;
    bool terminated_empty_candidates = false;
    SearchRunStats search_stats;

    
    ui results_count = 0;
    float best_score = 0.0f, worst_score = 0.0f;
    double matched_edges_mean = 0.0;

    
    double rss_after_query_mb = -1.0;
    double rss_delta_mb = 0.0;
};

struct RunReport {
    JsonKV run_meta;          
    TimingKV global_timing;   
    JsonKV global_extra;      
    std::vector<QueryRichRecord> queries;
};

inline void addTiming(TimingKV& kv, const std::string& key, double ms) {
    kv.push_back(std::make_pair(key, ms));
}

inline void addKv(JsonKV& kv, const std::string& key, const std::string& value_literal) {
    
    kv.push_back(std::make_pair(key, value_literal));
}

inline bool hasEmptyCandidateSet(
    const std::vector<std::vector<VertexID> >& candidates,
    ui expected_count) {
    if (candidates.size() != expected_count) {
        return true;
    }
    for (size_t u = 0; u < candidates.size(); ++u) {
        if (candidates[u].empty()) {
            return true;
        }
    }
    return false;
}

inline std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

inline std::string jsonString(const std::string& s) {
    return std::string("\"") + jsonEscape(s) + std::string("\"");
}

inline std::string jsonBool(bool b) {
    return b ? "true" : "false";
}

inline std::string formatMs(double ms) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << ms;
    return oss.str();
}

inline std::string formatFloat(double v, int prec = 4) {
    if (std::isnan(v)) return "null";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

inline std::string formatInt(long long v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

inline std::string formatU64(uint64_t v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

inline double safeDiv(double num, double den) {
    if (den <= 0.0) return 0.0;
    return num / den;
}

struct SiblingLeafKey {
    VertexID parent;
    ui relation_id;
    bool leaf_to_parent;
};

struct SiblingLeafGroup {
    SiblingLeafKey key;
    std::vector<VertexID> leaves;
};

bool sameSiblingLeafKey(const SiblingLeafKey& a, const SiblingLeafKey& b) {
    return a.parent == b.parent &&
           a.relation_id == b.relation_id &&
           a.leaf_to_parent == b.leaf_to_parent;
}

bool getSiblingLeafKey(const QueryPattern& query, VertexID u, bool undirected_mode, SiblingLeafKey& key) {
    if (u >= query.num_vertices) {
        return false;
    }
    const size_t out_deg = query.out_neighbors[u].size();
    const size_t in_deg = query.in_neighbors[u].size();
    if (out_deg + in_deg != 1) {
        return false;
    }
    if (out_deg == 1) {
        key.parent = query.out_neighbors[u][0].neighbor_id;
        key.relation_id = query.out_neighbors[u][0].relation_id;
        key.leaf_to_parent = undirected_mode ? false : true;
        return true;
    }
    key.parent = query.in_neighbors[u][0].neighbor_id;
    key.relation_id = query.in_neighbors[u][0].relation_id;
    key.leaf_to_parent = false;
    return true;
}

std::vector<SiblingLeafGroup> buildSiblingLeafGroups(const QueryPattern& query, bool undirected_mode) {
    std::vector<SiblingLeafGroup> groups;
    for (VertexID u = 0; u < query.num_vertices; ++u) {
        SiblingLeafKey key;
        if (!getSiblingLeafKey(query, u, undirected_mode, key)) {
            continue;
        }
        bool added = false;
        for (size_t i = 0; i < groups.size(); ++i) {
            if (sameSiblingLeafKey(groups[i].key, key)) {
                groups[i].leaves.push_back(u);
                added = true;
                break;
            }
        }
        if (!added) {
            SiblingLeafGroup group;
            group.key = key;
            group.leaves.push_back(u);
            groups.push_back(group);
        }
    }

    return groups;
}









void applySiblingLeafCap(const QueryPattern& query,
                         ui top_k,
                         bool undirected_mode,
                         bool debug_mode,
                         const EmbeddingDiGraph& kg,
                         const std::vector<ui>& rel_q2k,
                         std::vector<std::vector<VertexID> >& candidates,
                         std::vector<std::unordered_map<VertexID, float> >& ub_table) {
    std::string leaf_cap_env;
#ifdef _WIN32
    char* leaf_cap_env_buf = NULL;
    size_t leaf_cap_env_len = 0;
    if (_dupenv_s(&leaf_cap_env_buf, &leaf_cap_env_len, "FUSE_ENABLE_LEAF_CAP") == 0 &&
        leaf_cap_env_buf != NULL) {
        leaf_cap_env.assign(leaf_cap_env_buf);
        std::free(leaf_cap_env_buf);
    }
#else
    const char* leaf_cap_env_raw = std::getenv("FUSE_ENABLE_LEAF_CAP");
    if (leaf_cap_env_raw != NULL) {
        leaf_cap_env.assign(leaf_cap_env_raw);
    }
#endif
    if (!leaf_cap_env.empty()) {
        std::string value(leaf_cap_env);
        for (size_t i = 0; i < value.size(); ++i) {
            value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
        }
        if (value == "0" || value == "false" || value == "off" || value == "no") {
            if (debug_mode) {
                std::cerr << "[sibling-leaf-cap] disabled by FUSE_ENABLE_LEAF_CAP="
                          << leaf_cap_env << std::endl;
            }
            return;
        }
    }

    if (top_k == 0 || candidates.size() != query.num_vertices || ub_table.size() != query.num_vertices) {
        return;
    }

    std::vector<SiblingLeafGroup> groups = buildSiblingLeafGroups(query, undirected_mode);
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const SiblingLeafGroup& group = groups[gi];
        const size_t cap = std::max(group.leaves.size(), static_cast<size_t>(top_k));
        if (cap == 0) {
            continue;
        }

        VertexID parent = group.key.parent;
        if (parent >= candidates.size()) {
            continue;
        }

        ui mapped_rel = std::numeric_limits<ui>::max();
        if (group.key.relation_id < rel_q2k.size()) {
            mapped_rel = rel_q2k[group.key.relation_id];
        }
        if (mapped_rel == std::numeric_limits<ui>::max()) {
            continue;
        }

        std::vector<std::unordered_set<VertexID> > keep_by_leaf(group.leaves.size());
        std::vector<std::unordered_set<VertexID> > leaf_candidate_sets(group.leaves.size());
        for (size_t li = 0; li < group.leaves.size(); ++li) {
            keep_by_leaf[li].reserve(cap * candidates[parent].size() + 16);
            VertexID u = group.leaves[li];
            if (u < candidates.size()) {
                leaf_candidate_sets[li].reserve(candidates[u].size() * 2 + 16);
                for (size_t ci = 0; ci < candidates[u].size(); ++ci) {
                    leaf_candidate_sets[li].insert(candidates[u][ci]);
                }
            }
        }

        for (size_t pi = 0; pi < candidates[parent].size(); ++pi) {
            VertexID v_parent = candidates[parent][pi];

            std::vector<VertexID> parent_neighbors;
            parent_neighbors.reserve(cap * 4 + 16);
            auto collect_one_direction = [&](const VertexID* nbs, const ui* eids, ui edge_count) {
                for (ui ei = 0; ei < edge_count; ++ei) {
                    if (kg.getEdgeRelationId(eids[ei]) == mapped_rel) {
                        parent_neighbors.push_back(nbs[ei]);
                    }
                }
            };

            if (group.key.leaf_to_parent) {
                ui in_count = 0;
                const VertexID* in_nbs = kg.getInNeighbors(v_parent, in_count);
                const ui* in_eids = kg.getInEdgeIds(v_parent, in_count);
                collect_one_direction(in_nbs, in_eids, in_count);
                if (undirected_mode) {
                    ui out_count = 0;
                    const VertexID* out_nbs = kg.getOutNeighbors(v_parent, out_count);
                    const ui* out_eids = kg.getOutEdgeIds(v_parent, out_count);
                    collect_one_direction(out_nbs, out_eids, out_count);
                }
            } else {
                ui out_count = 0;
                const VertexID* out_nbs = kg.getOutNeighbors(v_parent, out_count);
                const ui* out_eids = kg.getOutEdgeIds(v_parent, out_count);
                collect_one_direction(out_nbs, out_eids, out_count);
                if (undirected_mode) {
                    ui in_count = 0;
                    const VertexID* in_nbs = kg.getInNeighbors(v_parent, in_count);
                    const ui* in_eids = kg.getInEdgeIds(v_parent, in_count);
                    collect_one_direction(in_nbs, in_eids, in_count);
                }
            }

            if (parent_neighbors.empty()) {
                continue;
            }

            for (size_t li = 0; li < group.leaves.size(); ++li) {
                VertexID u = group.leaves[li];
                if (u >= candidates.size() || candidates[u].size() <= cap) {
                    continue;
                }

                std::vector<std::pair<VertexID, float> > scored;
                scored.reserve(parent_neighbors.size());
                for (size_t ni = 0; ni < parent_neighbors.size(); ++ni) {
                    VertexID v_child = parent_neighbors[ni];
                    if (!leaf_candidate_sets[li].count(v_child)) {
                        continue;
                    }
                    std::unordered_map<VertexID, float>::const_iterator it = ub_table[u].find(v_child);
                    if (it != ub_table[u].end()) {
                        scored.push_back(std::make_pair(v_child, it->second));
                    }
                }
                if (scored.empty()) {
                    continue;
                }

                if (scored.size() > cap) {
                    std::nth_element(scored.begin(),
                                     scored.begin() + static_cast<std::ptrdiff_t>(cap),
                                     scored.end(),
                                     [](const std::pair<VertexID, float>& a, const std::pair<VertexID, float>& b) {
                                         if (a.second != b.second) {
                                             return a.second > b.second;
                                         }
                                         return a.first < b.first;
                                     });
                    scored.resize(cap);
                } else {
                    std::sort(scored.begin(), scored.end(),
                              [](const std::pair<VertexID, float>& a, const std::pair<VertexID, float>& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                }
                for (size_t si = 0; si < scored.size(); ++si) {
                    keep_by_leaf[li].insert(scored[si].first);
                }
            }
        }

        for (size_t li = 0; li < group.leaves.size(); ++li) {
            VertexID u = group.leaves[li];
            if (u >= candidates.size() || candidates[u].size() <= keep_by_leaf[li].size() || keep_by_leaf[li].empty()) {
                continue;
            }

            std::vector<std::pair<VertexID, float> > kept;
            kept.reserve(keep_by_leaf[li].size());
            for (std::unordered_set<VertexID>::const_iterator it = keep_by_leaf[li].begin();
                 it != keep_by_leaf[li].end(); ++it) {
                std::unordered_map<VertexID, float>::const_iterator ub_it = ub_table[u].find(*it);
                if (ub_it != ub_table[u].end()) {
                    kept.push_back(std::make_pair(*it, ub_it->second));
                }
            }
            std::sort(kept.begin(), kept.end(),
                      [](const std::pair<VertexID, float>& a, const std::pair<VertexID, float>& b) {
                          if (a.second != b.second) {
                              return a.second > b.second;
                          }
                          return a.first < b.first;
                      });

            candidates[u].clear();
            ub_table[u].clear();
            candidates[u].reserve(kept.size());
            for (size_t ki = 0; ki < kept.size(); ++ki) {
                candidates[u].push_back(kept[ki].first);
                ub_table[u][kept[ki].first] = kept[ki].second;
            }
            if (debug_mode) {
                std::cerr << "[sibling-leaf-cap] parent=" << group.key.parent
                          << " rel_q=" << group.key.relation_id
                          << " leaf_to_parent=" << (group.key.leaf_to_parent ? 1 : 0)
                          << " group_size=" << group.leaves.size()
                          << " parent_cands=" << candidates[parent].size()
                          << " leaf=" << u
                          << " cap=" << cap
                          << " kept=" << candidates[u].size() << std::endl;
            }
        }
    }
}




static const size_t kPerNodeListCap = 64;

inline void writeCandStatsJson(std::ofstream& f, const std::string& indent,
                               const CandStats& s, double drop_rate, bool has_drop) {
    f << "{ ";
    f << "\"sum\": " << formatU64(s.sum) << ", ";
    f << "\"cover_sum\": " << formatU64(s.cover_sum) << ", ";
    f << "\"independent_sum\": " << formatU64(s.independent_sum) << ", ";
    f << "\"mean\": " << formatFloat(s.mean, 3) << ", ";
    f << "\"max\": " << formatU64(s.max) << ", ";
    f << "\"min\": " << formatU64(s.min);
    if (has_drop) {
        f << ", \"drop_rate\": " << formatFloat(drop_rate, 4);
    }
    
    f << ",\n" << indent << "  \"per_node\": [";
    const size_t emit = std::min(s.per_node.size(), kPerNodeListCap);
    for (size_t i = 0; i < emit; ++i) {
        if (i) f << ", ";
        f << formatU64(s.per_node[i]);
    }
    f << "]";
    if (s.per_node.size() > kPerNodeListCap) {
        f << ",\n" << indent << "  \"per_node_truncated_to\": " << kPerNodeListCap;
    }
    f << " }";
}

void writeTimingJson(const std::string& path, const RunReport& rpt) {
    std::ofstream f(path.c_str());
    if (!f.is_open()) {
        std::cerr << "[warn] cannot write timing json to " << path << std::endl;
        return;
    }
    f << "{\n";

    
    
    f << "  \"run_meta\": {\n";
    for (size_t i = 0; i < rpt.run_meta.size(); ++i) {
        f << "    \"" << jsonEscape(rpt.run_meta[i].first) << "\": "
          << rpt.run_meta[i].second;
        if (i + 1 < rpt.run_meta.size()) f << ",";
        f << "\n";
    }
    f << "  },\n";

    
    f << "  \"global\": {\n";
    bool first_global = true;
    for (size_t i = 0; i < rpt.global_timing.size(); ++i) {
        if (!first_global) f << ",\n";
        first_global = false;
        f << "    \"" << jsonEscape(rpt.global_timing[i].first) << "\": "
          << formatMs(rpt.global_timing[i].second);
    }
    for (size_t i = 0; i < rpt.global_extra.size(); ++i) {
        if (!first_global) f << ",\n";
        first_global = false;
        f << "    \"" << jsonEscape(rpt.global_extra[i].first) << "\": "
          << rpt.global_extra[i].second;
    }
    if (!first_global) f << "\n";
    f << "  },\n";

    f << "  \"queries\": {\n";
    for (size_t qi = 0; qi < rpt.queries.size(); ++qi) {
        const QueryRichRecord& q = rpt.queries[qi];
        f << "    \"" << jsonEscape(q.qname) << "\": {\n";

        
        for (size_t si = 0; si < q.steps.size(); ++si) {
            f << "      \"" << jsonEscape(q.steps[si].first) << "\": "
              << formatMs(q.steps[si].second) << ",\n";
        }

        
        f << "      \"structure\": { ";
        f << "\"raw_vertices\": "    << formatInt(q.raw_vertices)    << ", ";
        f << "\"dedup_vertices\": "  << formatInt(q.dedup_vertices)  << ", ";
        f << "\"edges\": "           << formatInt(q.edges)           << ", ";
        f << "\"cover_size\": "      << formatInt(q.cover_size)      << ", ";
        f << "\"independent_size\": "<< formatInt(q.independent_size)<< ", ";
        f << "\"max_degree\": "      << formatInt(q.max_degree)      << ", ";
        f << "\"root\": "            << formatInt(q.root);
        f << " },\n";

        
        f << "      \"vocab\": { ";
        f << "\"q_relations\": "                  << formatInt(q.q_relations)                  << ", ";
        f << "\"q_relations_unmapped\": "         << formatInt(q.q_relations_unmapped)         << ", ";
        f << "\"q_labels\": "                     << formatInt(q.q_labels)                     << ", ";
        f << "\"q_labels_unmapped\": "            << formatInt(q.q_labels_unmapped)            << ", ";
        f << "\"q_nodes_no_label_intersection\": "<< formatInt(q.q_nodes_no_label_intersection);
        f << " },\n";

        
        f << "      \"candidates\": {\n";
        f << "        \"after_init\": ";
        writeCandStatsJson(f, "        ", q.cands_after_init, 0.0, false);
        f << ",\n";

        const double init_sum   = static_cast<double>(q.cands_after_init.sum);
        const double refine_sum = static_cast<double>(q.cands_after_refine.sum);
        const double prune_sum  = static_cast<double>(q.cands_after_prune.sum);

        f << "        \"after_refine\": ";
        writeCandStatsJson(f, "        ", q.cands_after_refine,
                           1.0 - safeDiv(refine_sum, init_sum), true);
        f << ",\n";

        f << "        \"after_prune\": ";
        writeCandStatsJson(f, "        ", q.cands_after_prune,
                           1.0 - safeDiv(prune_sum, refine_sum), true);
        f << "\n";
        f << "      },\n";

        
        f << "      \"ub_table\": { ";
        f << "\"cells_after_compute\": " << formatU64(q.ub_cells_after_compute) << ", ";
        f << "\"cells_after_prune\": "   << formatU64(q.ub_cells_after_prune);
        f << " },\n";

        
        f << "      \"cand_init\": { ";
        f << "\"pool_size\": "        << formatU64(q.cand_init_stats.pool_size)        << ", ";
        f << "\"kept_by_degree\": "   << formatU64(q.cand_init_stats.kept_by_degree)   << ", ";
        f << "\"sim_computations\": " << formatU64(q.cand_init_stats.sim_computations) << ", ";
        f << "\"kept_by_topn\": "     << formatU64(q.cand_init_stats.kept_by_topn)     << ", ";
        f << "\"wildcard_nodes\": "   << formatU64(q.cand_init_stats.wildcard_nodes);
        f << " },\n";

        
        f << "      \"refine\": { ";
        f << "\"iters_actual\": " << formatInt(q.refine_iters_actual) << ", ";
        f << "\"terminated_empty_candidates\": " << jsonBool(q.terminated_empty_candidates);
        f << " },\n";

        
        f << "      \"search\": { ";
        f << "\"states_pushed\": " << formatU64(q.search_stats.states_pushed) << ", ";
        f << "\"states_popped\": " << formatU64(q.search_stats.states_popped);
        f << " },\n";

        
        f << "      \"results\": { ";
        f << "\"count\": "             << formatInt(q.results_count)            << ", ";
        f << "\"best_score\": "        << formatFloat(q.best_score, 6)          << ", ";
        f << "\"worst_score\": "       << formatFloat(q.worst_score, 6)         << ", ";
        f << "\"matched_edges_mean\": "<< formatFloat(q.matched_edges_mean, 3);
        f << " },\n";

        
        f << "      \"memory\": { ";
        f << "\"rss_after_query_mb\": " << formatFloat(q.rss_after_query_mb, 2) << ", ";
        f << "\"rss_delta_mb\": "       << formatFloat(q.rss_delta_mb, 2);
        f << " }\n";

        f << "    }";
        if (qi + 1 < rpt.queries.size()) f << ",";
        f << "\n";
    }
    f << "  }\n";
    f << "}\n";
    f.close();
}

inline bool isAllDigits(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

inline bool isRegularFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
}

inline bool isDirectory(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::path(path), ec);
}

inline std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) {
        return file;
    }
    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') {
        return dir + file;
    }
    return dir + "/" + file;
}

inline bool looksLikeGraphFolder(const std::string& path) {
    return isRegularFile(joinPath(path, "FactKG.graph.txt")) ||
           (isRegularFile(joinPath(path, "edges.txt")) && isRegularFile(joinPath(path, "stats.txt")));
}

struct QueryEntry {
    int series_rank;
    int id;
    std::string name;
    std::string series;
    std::string source_name;
    std::string path;
};

MaterializedQuery buildMaterializedQuery(const EmbeddingDiGraph& query_graph, const std::string& query_path) {
    const ui original_qv = query_graph.getVerticesCount();
    std::unordered_map<std::string, VertexID> name_to_new_id;
    name_to_new_id.reserve(original_qv);

    std::vector<VertexID> old_to_new(original_qv, std::numeric_limits<VertexID>::max());
    std::vector<VertexID> new_to_representative_old;
    new_to_representative_old.reserve(original_qv);

    for (VertexID old_u = 0; old_u < original_qv; ++old_u) {
        const std::string& name = query_graph.getVertexName(old_u);
        std::unordered_map<std::string, VertexID>::const_iterator it = name_to_new_id.find(name);
        if (it == name_to_new_id.end()) {
            VertexID new_id = static_cast<VertexID>(new_to_representative_old.size());
            name_to_new_id[name] = new_id;
            old_to_new[old_u] = new_id;
            new_to_representative_old.push_back(old_u);
        } else {
            old_to_new[old_u] = it->second;
        }
    }

    std::vector<QueryDirectedEdge> remapped_edges;
    remapped_edges.reserve(query_graph.getEdgesCount());

    
    std::ifstream edge_file(joinPath(query_path, "edges.txt").c_str());
    if (edge_file.is_open()) {
        while (true) {
            VertexID old_src = 0;
            ui rel = 0;
            VertexID old_dst = 0;
            if (!(edge_file >> old_src >> rel >> old_dst)) {
                break;
            }
            if (old_src >= original_qv || old_dst >= original_qv) {
                continue;
            }
            remapped_edges.push_back(QueryDirectedEdge{old_to_new[old_src], old_to_new[old_dst], rel});
        }
        edge_file.close();
    }

    if (remapped_edges.empty()) {
        for (VertexID old_u = 0; old_u < original_qv; ++old_u) {
            ui out_count = 0;
            const VertexID* out_nbs = query_graph.getOutNeighbors(old_u, out_count);
            const ui* out_eids = query_graph.getOutEdgeIds(old_u, out_count);
            for (ui i = 0; i < out_count; ++i) {
                ui rel = query_graph.getEdgeRelationId(out_eids[i]);
                remapped_edges.push_back(QueryDirectedEdge{old_to_new[old_u], old_to_new[out_nbs[i]], rel});
            }
        }
    }

    MaterializedQuery out;
    out.new_count = static_cast<ui>(new_to_representative_old.size());
    out.pattern = VertexCoverRefiner::buildQueryPattern(out.new_count, remapped_edges);
    out.edges_in_query_order.swap(remapped_edges);
    out.node_embeddings.resize(out.new_count, NULL);
    for (ui new_u = 0; new_u < out.new_count; ++new_u) {
        out.node_embeddings[new_u] = query_graph.getVertexEmbedding(new_to_representative_old[new_u]);
    }
    out.old_to_new = std::move(old_to_new);
    return out;
}
}

int runOneQueryChild(const QueryEntry& entry,
                     const std::string& temp_dir,
                     const std::string& timing_file,
                     const EmbeddingDiGraph& kg,
                     const VamanaANN& ann,
                     const std::unordered_map<std::string, ui>& kg_rel_name_to_id,
                     const std::unordered_map<std::string, LabelID>& kg_label_name_to_id,
                     ui top_k,
                     ui top_n,
                     bool undirected_mode,
                     bool debug_mode) {
    RunReport run;
    addKv(run.run_meta, "query",                jsonString(entry.source_name));
    addKv(run.run_meta, "query_path",           jsonString(entry.path));
    addKv(run.run_meta, "top_k",                formatInt(static_cast<long long>(top_k)));
    addKv(run.run_meta, "top_n",                formatInt(static_cast<long long>(top_n)));
    addKv(run.run_meta, "undirected",           jsonBool(undirected_mode));
    addKv(run.run_meta, "graph_format",         jsonString("new_graph"));
    addKv(run.run_meta, "debug",                jsonBool(debug_mode));

    Stopwatch sw_total;
    const double rss_initial_mb = readVmRssMb();
    (void)rss_initial_mb;
    std::vector<QueryEntry> query_entries;
    query_entries.push_back(entry);

    Stopwatch sw_q;        
    Stopwatch sw_qstep;    

    for (size_t qi = 0; qi < query_entries.size(); ++qi) {
        int qid = query_entries[qi].id;
        const std::string& qname = query_entries[qi].name;
        std::string query_path = query_entries[qi].path;
        std::string query_load_path = query_path;

        QueryRichRecord qrec;
        qrec.qname = qname;
        qrec.qid = qid;
        const double rss_pre_query_mb = readVmRssMb();
        sw_q.reset();

        sw_qstep.reset();
        EmbeddingDiGraph query_graph(false, true);
        query_graph.loadFromKG(query_load_path, true);
        addTiming(qrec.steps, "query_load_ms", sw_qstep.elapsed_ms());
        qrec.raw_vertices = query_graph.getVerticesCount();
        if (query_graph.getVerticesCount() == 0 || query_graph.getRelationsCount() == 0) {
            std::ofstream bad_out(joinPath(temp_dir, qname + ".txt").c_str());
            bad_out.close();
            addTiming(qrec.steps, "total_ms", sw_q.elapsed_ms());
            qrec.rss_after_query_mb = readVmRssMb();
            qrec.rss_delta_mb = (qrec.rss_after_query_mb >= 0 && rss_pre_query_mb >= 0)
                ? (qrec.rss_after_query_mb - rss_pre_query_mb) : 0.0;
            run.queries.push_back(std::move(qrec));
            continue;
        }

        sw_qstep.reset();
        MaterializedQuery mq = buildMaterializedQuery(query_graph, query_load_path);
        QueryPattern query = mq.pattern;
        addTiming(qrec.steps, "materialize_ms", sw_qstep.elapsed_ms());
        
        qrec.dedup_vertices = mq.new_count;
        qrec.edges          = static_cast<ui>(mq.edges_in_query_order.size());
        qrec.max_degree = 0;
        for (ui u = 0; u < query.num_vertices; ++u) {
            const ui deg = static_cast<ui>(query.out_neighbors[u].size() + query.in_neighbors[u].size());
            if (deg > qrec.max_degree) qrec.max_degree = deg;
        }

        sw_qstep.reset();
        QueryVocabMap vocab = buildQueryVocabMap(
            query_graph, mq.old_to_new, mq.new_count, kg_rel_name_to_id, kg_label_name_to_id);
        addTiming(qrec.steps, "vocab_map_ms", sw_qstep.elapsed_ms());
        
        qrec.q_relations = static_cast<ui>(vocab.rel_q2k.size());
        qrec.q_labels    = static_cast<ui>(vocab.label_q2k.size());
        qrec.q_relations_unmapped = 0;
        for (size_t r = 0; r < vocab.rel_q2k.size(); ++r) {
            if (vocab.rel_q2k[r] == std::numeric_limits<ui>::max()) {
                qrec.q_relations_unmapped += 1;
            }
        }
        qrec.q_labels_unmapped = 0;
        for (size_t l = 0; l < vocab.label_q2k.size(); ++l) {
            if (vocab.label_q2k[l] == static_cast<LabelID>(std::numeric_limits<ui>::max())) {
                qrec.q_labels_unmapped += 1;
            }
        }
        
        qrec.q_nodes_no_label_intersection = 0;

        VertexCoverRefiner refiner(kg, debug_mode);
        refiner.setUndirectedMode(undirected_mode);
        refiner.setDebugMode(debug_mode);
        refiner.setRelationNameMap(vocab.rel_q2k);
        refiner.setVamanaIndex(&ann);

        sw_qstep.reset();
        std::unordered_set<VertexID> cover_set = refiner.computeMinVertexCover(query);
        if (cover_set.empty()) cover_set.insert(0);
        std::unordered_set<VertexID> independent_set =
            VertexCoverRefiner::buildIndependentSet(query.num_vertices, cover_set);
        addTiming(qrec.steps, "compute_min_cover_ms", sw_qstep.elapsed_ms());
        qrec.cover_size       = static_cast<ui>(cover_set.size());
        qrec.independent_size = static_cast<ui>(independent_set.size());

        sw_qstep.reset();
        std::vector<std::vector<VertexID> > candidates;
        refiner.initializeCandidatesForCover(query, cover_set, candidates,
                                             top_n,
                                             mq.node_embeddings,
                                             0);
        addTiming(qrec.steps, "init_candidates_ms", sw_qstep.elapsed_ms());
        qrec.cands_after_init = summarizeCandidates(candidates, cover_set);
        qrec.cand_init_stats = refiner.getLastCandInitStats();

        sw_qstep.reset();
        refiner.iterativeRefinement(query, cover_set, independent_set, candidates, 20, 0);
        addTiming(qrec.steps, "iterative_refinement_ms", sw_qstep.elapsed_ms());
        qrec.cands_after_refine = summarizeCandidates(candidates, cover_set);
        qrec.refine_iters_actual = refiner.getLastRefineIters();

        if (hasEmptyCandidateSet(candidates, query.num_vertices)) {
            qrec.terminated_empty_candidates = true;
            qrec.cands_after_prune = qrec.cands_after_refine;
            addTiming(qrec.steps, "select_root_ms", 0.0);
            addTiming(qrec.steps, "build_bfs_dag_ms", 0.0);
            addTiming(qrec.steps, "compute_ub_ms", 0.0);
            addTiming(qrec.steps, "prune_top_k_ms", 0.0);
            addTiming(qrec.steps, "search_topk_ms", 0.0);

            sw_qstep.reset();
            std::ofstream empty_out(joinPath(temp_dir, qname + ".txt").c_str());
            empty_out.close();
            addTiming(qrec.steps, "write_ms", sw_qstep.elapsed_ms());

            addTiming(qrec.steps, "total_ms", sw_q.elapsed_ms());
            qrec.rss_after_query_mb = readVmRssMb();
            qrec.rss_delta_mb = (qrec.rss_after_query_mb >= 0 && rss_pre_query_mb >= 0)
                ? (qrec.rss_after_query_mb - rss_pre_query_mb) : 0.0;
            run.queries.push_back(std::move(qrec));
            continue;
        }

        UpperBoundPruner ub(kg, debug_mode);
        ub.setUndirectedMode(undirected_mode);
        ub.setRelationNameMap(vocab.rel_q2k);

        sw_qstep.reset();
        VertexID root = ub.selectRootByCandidateSet(query, candidates);
        addTiming(qrec.steps, "select_root_ms", sw_qstep.elapsed_ms());
        qrec.root = static_cast<int>(root);

        sw_qstep.reset();
        std::vector<VertexID> dfs_order;
        std::vector<std::vector<UpperBoundPruner::DagEdge> > dag_children;
        ub.buildDfsAndDag(query, root, dfs_order, dag_children);
        addTiming(qrec.steps, "build_bfs_dag_ms", sw_qstep.elapsed_ms());

        sw_qstep.reset();
        std::vector<std::unordered_map<VertexID, float> > ub_table;
        ub.computeUpperBounds(query, mq.node_embeddings, dfs_order, dag_children, candidates, ub_table);
        addTiming(qrec.steps, "compute_ub_ms", sw_qstep.elapsed_ms());
        qrec.ub_cells_after_compute = 0;
        for (size_t u = 0; u < ub_table.size(); ++u) {
            qrec.ub_cells_after_compute += static_cast<uint64_t>(ub_table[u].size());
        }

        sw_qstep.reset();
        
        
        
        
        
        applySiblingLeafCap(query, top_k, undirected_mode, debug_mode, kg, vocab.rel_q2k, candidates, ub_table);
        addTiming(qrec.steps, "prune_top_k_ms", sw_qstep.elapsed_ms());
        qrec.cands_after_prune = summarizeCandidates(candidates, cover_set);
        qrec.ub_cells_after_prune = 0;
        for (size_t u = 0; u < ub_table.size(); ++u) {
            qrec.ub_cells_after_prune += static_cast<uint64_t>(ub_table[u].size());
        }

        GlobalSearcher searcher(kg, debug_mode);
        searcher.setUndirectedMode(undirected_mode);
        searcher.setRelationNameMap(vocab.rel_q2k);

        
        std::vector<VertexID> search_order = dfs_order;
        if (debug_mode) {
            std::cerr << "[search-order]";
            for (size_t i = 0; i < search_order.size(); ++i) {
                std::cerr << " " << search_order[i] << "(c=" << candidates[search_order[i]].size() << ")";
            }
            std::cerr << std::endl;
        }

        sw_qstep.reset();
        std::vector<GlobalSearcher::SearchResult> results =
            searcher.searchTopK(query, mq.node_embeddings, search_order, dag_children, candidates, ub_table, top_k, &mq.edges_in_query_order);
        addTiming(qrec.steps, "search_topk_ms", sw_qstep.elapsed_ms());
        qrec.search_stats = searcher.getLastSearchStats();
        qrec.results_count = static_cast<ui>(results.size());
        if (!results.empty()) {
            qrec.best_score  = results.front().score;
            qrec.worst_score = results.back().score;
            uint64_t total_edges = 0;
            for (size_t i = 0; i < results.size(); ++i) {
                total_edges += static_cast<uint64_t>(results[i].matched_edges.size());
            }
            qrec.matched_edges_mean =
                static_cast<double>(total_edges) / static_cast<double>(results.size());
        }

        sw_qstep.reset();
        std::ofstream fout(joinPath(temp_dir, qname + ".txt").c_str());
        fout << std::setprecision(8);
        for (size_t i = 0; i < results.size(); ++i) {
            fout << results[i].score;
            for (size_t e = 0; e < results[i].matched_edges.size(); ++e) {
                const std::string& msrc_name = std::get<0>(results[i].matched_edges[e]);
                const std::string& mrel_name = std::get<1>(results[i].matched_edges[e]);
                const std::string& mdst_name = std::get<2>(results[i].matched_edges[e]);

                
                std::unordered_map<std::string, ui>::const_iterator it_rel =
                    kg_rel_name_to_id.find(mrel_name);
                ui mrel_id = (it_rel != kg_rel_name_to_id.end())
                    ? it_rel->second
                    : std::numeric_limits<ui>::max();

                
                VertexID msrc_id = std::numeric_limits<VertexID>::max();
                VertexID mdst_id = std::numeric_limits<VertexID>::max();
                if (e < mq.edges_in_query_order.size()) {
                    const QueryDirectedEdge& qe = mq.edges_in_query_order[e];
                    if (qe.src < results[i].mapping.size()) {
                        msrc_id = results[i].mapping[qe.src];
                    }
                    if (qe.dst < results[i].mapping.size()) {
                        mdst_id = results[i].mapping[qe.dst];
                    }
                    
                    if (msrc_id != std::numeric_limits<VertexID>::max() &&
                        mdst_id != std::numeric_limits<VertexID>::max() &&
                        kg.getVertexName(msrc_id) != msrc_name) {
                        std::swap(msrc_id, mdst_id);
                    }
                }

                fout << "|" << msrc_id << "---" << mrel_id << "---" << mdst_id;
                fout << "|" << msrc_name << "---" << mrel_name << "---" << mdst_name;
            }
            fout << "\n";
        }
        fout.close();
        addTiming(qrec.steps, "write_ms", sw_qstep.elapsed_ms());

        addTiming(qrec.steps, "total_ms", sw_q.elapsed_ms());
        qrec.rss_after_query_mb = readVmRssMb();
        qrec.rss_delta_mb = (qrec.rss_after_query_mb >= 0 && rss_pre_query_mb >= 0)
            ? (qrec.rss_after_query_mb - rss_pre_query_mb) : 0.0;
        run.queries.push_back(std::move(qrec));
    }

    const double total_ms_d = sw_total.elapsed_ms();
    addTiming(run.global_timing, "queries_processed", static_cast<double>(run.queries.size()));
    addTiming(run.global_timing, "total_ms", total_ms_d);
    addKv(run.global_extra, "mem_rss_peak_mb", formatFloat(readVmPeakMb(), 2));
    writeTimingJson(timing_file, run);
    return 0;
}

namespace {
struct ParentQuerySummary {
    std::string name;
    std::string series;
    std::string source_name;
    int id = -1;
    double elapsed_ms = 0.0;
    std::string status = "FAILED";
    bool timed_out = false;
    bool exited = false;
    int exit_code = -1;
    int signal_no = 0;
};

std::vector<ui> parseUiList(const std::string& text) {
    std::vector<ui> values;
    std::set<ui> seen;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        size_t first = 0;
        while (first < token.size() && std::isspace(static_cast<unsigned char>(token[first]))) {
            ++first;
        }
        size_t last = token.size();
        while (last > first && std::isspace(static_cast<unsigned char>(token[last - 1]))) {
            --last;
        }
        if (first == last) {
            throw std::invalid_argument("empty candidate budget");
        }
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(token.substr(first, last - first), &consumed);
        if (consumed != last - first || parsed == 0 || parsed > std::numeric_limits<ui>::max()) {
            throw std::invalid_argument("invalid candidate budget");
        }
        const ui value = static_cast<ui>(parsed);
        if (seen.insert(value).second) {
            values.push_back(value);
        }
    }
    if (values.empty()) {
        throw std::invalid_argument("candidate budget list is empty");
    }
    return values;
}

int seriesRank(const std::string& name) {
    if (name.size() > 1 && (name[0] == 'n' || name[0] == 'N') && isAllDigits(name.substr(1))) {
        return std::atoi(name.c_str() + 1);
    }
    return std::numeric_limits<int>::max();
}

std::vector<std::string> listDirectoryNames(const std::string& root) {
    std::vector<std::string> names;
    std::error_code ec;
    std::filesystem::directory_iterator it(std::filesystem::path(root), ec);
    const std::filesystem::directory_iterator end;
    if (ec) {
        return names;
    }
    while (it != end) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && name[0] != '.') {
            names.push_back(name);
        }
        it.increment(ec);
        if (ec) {
            break;
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void addQueryEntry(std::vector<QueryEntry>& entries,
                   const std::string& series,
                   const std::string& name,
                   const std::string& path) {
    if (!isAllDigits(name) || !isDirectory(path) || !looksLikeGraphFolder(path)) {
        return;
    }
    QueryEntry entry;
    entry.series_rank = series == "queries" ? -1 : seriesRank(series);
    entry.id = std::atoi(name.c_str());
    entry.name = name;
    entry.series = series;
    entry.source_name = series == "queries" ? name : joinPath(series, name);
    entry.path = path;
    entries.push_back(entry);
}

std::vector<QueryEntry> collectQueryEntries(const std::string& queries_root) {
    std::vector<QueryEntry> entries;
    const std::vector<std::string> top_names = listDirectoryNames(queries_root);
    for (size_t i = 0; i < top_names.size(); ++i) {
        const std::string top_path = joinPath(queries_root, top_names[i]);
        if (!isDirectory(top_path)) {
            continue;
        }
        if (isAllDigits(top_names[i])) {
            addQueryEntry(entries, "queries", top_names[i], top_path);
            continue;
        }
        const std::vector<std::string> child_names = listDirectoryNames(top_path);
        for (size_t j = 0; j < child_names.size(); ++j) {
            addQueryEntry(entries, top_names[i], child_names[j], joinPath(top_path, child_names[j]));
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const QueryEntry& a, const QueryEntry& b) {
                  if (a.series_rank != b.series_rank) {
                      return a.series_rank < b.series_rank;
                  }
                  if (a.series != b.series) {
                      return a.series < b.series;
                  }
                  if (a.id != b.id) {
                      return a.id < b.id;
                  }
                  return a.name < b.name;
              });
    return entries;
}

std::vector<std::string> collectSeriesNames(const std::vector<QueryEntry>& entries) {
    std::vector<std::string> names;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (std::find(names.begin(), names.end(), entries[i].series) == names.end()) {
            names.push_back(entries[i].series);
        }
    }
    return names;
}

std::vector<QueryEntry> entriesForSeries(const std::vector<QueryEntry>& entries,
                                         const std::string& series) {
    std::vector<QueryEntry> selected;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].series == series) {
            selected.push_back(entries[i]);
        }
    }
    return selected;
}

size_t countEntriesForSeries(const std::vector<QueryEntry>& entries,
                             const std::string& series) {
    size_t count = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].series == series) {
            ++count;
        }
    }
    return count;
}

bool ensureDirectory(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::path(path), ec)) {
        return true;
    }
    ec.clear();
    return std::filesystem::create_directories(std::filesystem::path(path), ec) && !ec;
}

std::string uniquePath(const std::string& base) {
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(base), ec)) {
        return base;
    }
    for (uint64_t suffix = 1; ; ++suffix) {
        std::ostringstream candidate;
        candidate << base << "_" << suffix;
        ec.clear();
        if (!std::filesystem::exists(std::filesystem::path(candidate.str()), ec)) {
            return candidate.str();
        }
    }
}

std::string seriesRoot(const std::string& output_root, ui top_n, const std::string& series) {
    std::ostringstream budget;
    budget << "topn" << top_n;
    return joinPath(joinPath(output_root, series), budget.str());
}

std::string makeTempDirectory(const std::string& root, ui top_k, ui top_n) {
    std::ostringstream name;
    name << ".tmp_K" << top_k << "_nodetopk" << top_n << "_pid" << static_cast<long>(getpid());
    return uniquePath(joinPath(root, name.str()));
}

std::string makeFinalDirectory(const std::string& root,
                               ui top_k,
                               ui top_n,
                               bool undirected_mode,
                               double elapsed_ms) {
    std::ostringstream name;
    name << "pipeline_K" << top_k
         << "_nodetopk" << top_n
         << "_undirected" << (undirected_mode ? 1 : 0)
         << "_" << static_cast<long long>(elapsed_ms) << "ms";
    return uniquePath(joinPath(root, name.str()));
}

ParentQuerySummary waitForChild(pid_t pid,
                                const QueryEntry& entry,
                                unsigned long timeout_sec) {
    ParentQuerySummary summary;
    summary.name = entry.name;
    summary.series = entry.series;
    summary.source_name = entry.source_name;
    summary.id = entry.id;
    Stopwatch stopwatch;
    int status = 0;
    while (true) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            summary.elapsed_ms = stopwatch.elapsed_ms();
            if (WIFEXITED(status)) {
                summary.exited = true;
                summary.exit_code = WEXITSTATUS(status);
                summary.status = summary.exit_code == 0 ? "DONE" : "FAILED";
            } else if (WIFSIGNALED(status)) {
                summary.signal_no = WTERMSIG(status);
                summary.status = "FAILED";
            }
            return summary;
        }
        if (waited < 0 && errno != EINTR) {
            summary.elapsed_ms = stopwatch.elapsed_ms();
            summary.signal_no = -1;
            summary.status = "FAILED";
            return summary;
        }
        if (timeout_sec > 0 &&
            stopwatch.elapsed_ms() >= static_cast<double>(timeout_sec) * 1000.0) {
            summary.timed_out = true;
            summary.status = "TIMEOUT";
            kill(pid, SIGTERM);
            bool reaped = false;
            for (int attempt = 0; attempt < 50; ++attempt) {
                const pid_t terminated = waitpid(pid, &status, WNOHANG);
                if (terminated == pid) {
                    reaped = true;
                    break;
                }
                usleep(10000);
            }
            if (!reaped) {
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
            }
            summary.elapsed_ms = stopwatch.elapsed_ms();
            if (WIFEXITED(status)) {
                summary.exited = true;
                summary.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                summary.signal_no = WTERMSIG(status);
            }
            return summary;
        }
        usleep(10000);
    }
}

void writeFailureTiming(const std::string& path,
                        const QueryEntry& entry,
                        ui top_k,
                        ui top_n,
                        bool undirected_mode,
                        unsigned long timeout_sec,
                        const ParentQuerySummary& summary) {
    std::ofstream file(path.c_str());
    if (!file.is_open()) {
        return;
    }
    file << "{\n";
    file << "  \"run_meta\": {\n";
    file << "    \"query\": " << jsonString(entry.source_name) << ",\n";
    file << "    \"query_path\": " << jsonString(entry.path) << ",\n";
    file << "    \"top_k\": " << top_k << ",\n";
    file << "    \"top_n\": " << top_n << ",\n";
    file << "    \"undirected\": " << jsonBool(undirected_mode) << ",\n";
    file << "    \"graph_format\": \"new_graph\",\n";
    file << "    \"query_time_limit_sec\": " << timeout_sec << "\n";
    file << "  },\n";
    file << "  \"queries\": {\n";
    file << "    \"" << jsonEscape(entry.name) << "\": {\n";
    file << "      \"total_ms\": " << formatMs(summary.elapsed_ms) << ",\n";
    file << "      \"status\": " << jsonString(summary.status) << ",\n";
    file << "      \"timed_out\": " << jsonBool(summary.timed_out) << ",\n";
    file << "      \"exit_code\": " << summary.exit_code << ",\n";
    file << "      \"signal\": " << summary.signal_no << "\n";
    file << "    }\n";
    file << "  }\n";
    file << "}\n";
}

bool isSuccessful(const ParentQuerySummary& summary) {
    return summary.status == "DONE" && summary.exited && summary.exit_code == 0 && !summary.timed_out;
}

void ensureFailureArtifacts(const std::string& temp_dir,
                            const std::string& timing_dir,
                            const QueryEntry& entry,
                            ui top_k,
                            ui top_n,
                            bool undirected_mode,
                            unsigned long timeout_sec,
                            const ParentQuerySummary& summary) {
    std::ofstream result(joinPath(temp_dir, entry.name + ".txt").c_str(), std::ios::trunc);
    result.close();
    writeFailureTiming(joinPath(timing_dir, entry.name + ".json"), entry, top_k, top_n,
                       undirected_mode, timeout_sec, summary);
}

void writeProgressHeader(const std::string& path) {
    std::ofstream file(path.c_str(), std::ios::trunc);
    file << "status\tquery\tsource\telapsed_ms\texit_code\tsignal\n";
}

void appendProgress(const std::string& path,
                    const std::string& status,
                    const QueryEntry& entry,
                    const ParentQuerySummary* summary) {
    std::ofstream file(path.c_str(), std::ios::app);
    file << status << "\t" << entry.name << "\t" << entry.source_name;
    if (summary == NULL) {
        file << "\t\t\t\n";
        return;
    }
    file << "\t" << formatMs(summary->elapsed_ms)
         << "\t" << summary->exit_code
         << "\t" << summary->signal_no << "\n";
}

void writeSeriesSummary(const std::string& path,
                        const std::string& kg_path,
                        const std::string& queries_root,
                        const std::string& output_root,
                        const std::string& series,
                        const std::string& ann_index_dir,
                        ui top_k,
                        ui top_n,
                        bool undirected_mode,
                        int start_id,
                        int end_id,
                        bool debug_mode,
                        unsigned long timeout_sec,
                        double kg_load_ms,
                        double ann_load_ms,
                        double vocab_ms,
                        double scan_ms,
                        double series_ms,
                        size_t queries_found,
                        const std::vector<ParentQuerySummary>& summaries,
                        const EmbeddingDiGraph& kg,
                        const VamanaANN& ann) {
    size_t ok_count = 0;
    size_t timeout_count = 0;
    for (size_t i = 0; i < summaries.size(); ++i) {
        if (isSuccessful(summaries[i])) {
            ++ok_count;
        }
        if (summaries[i].timed_out) {
            ++timeout_count;
        }
    }
    std::ofstream file(path.c_str());
    if (!file.is_open()) {
        throw std::runtime_error("cannot write series timing file");
    }
    file << "{\n";
    file << "  \"run_meta\": {\n";
    file << "    \"kg_path\": " << jsonString(kg_path) << ",\n";
    file << "    \"queries_root\": " << jsonString(queries_root) << ",\n";
    file << "    \"outputs_root\": " << jsonString(output_root) << ",\n";
    file << "    \"series\": " << jsonString(series) << ",\n";
    file << "    \"top_k\": " << top_k << ",\n";
    file << "    \"top_n\": " << top_n << ",\n";
    file << "    \"undirected\": " << jsonBool(undirected_mode) << ",\n";
    file << "    \"start_id\": " << start_id << ",\n";
    file << "    \"end_id\": " << end_id << ",\n";
    file << "    \"graph_format\": \"new_graph\",\n";
    file << "    \"debug\": " << jsonBool(debug_mode) << ",\n";
    file << "    \"query_time_limit_sec\": " << timeout_sec << "\n";
    file << "  },\n";
    file << "  \"global\": {\n";
    file << "    \"kg_load_ms\": " << formatMs(kg_load_ms) << ",\n";
    file << "    \"ann_build_ms\": " << formatMs(ann_load_ms) << ",\n";
    file << "    \"kg_vocab_table_ms\": " << formatMs(vocab_ms) << ",\n";
    file << "    \"queries_scan_ms\": " << formatMs(scan_ms) << ",\n";
    file << "    \"series_total_ms_excluding_kg_ann\": " << formatMs(series_ms) << ",\n";
    file << "    \"queries_found\": " << queries_found << ",\n";
    file << "    \"queries_processed\": " << summaries.size() << ",\n";
    file << "    \"queries_ok\": " << ok_count << ",\n";
    file << "    \"queries_failed\": " << summaries.size() - ok_count << ",\n";
    file << "    \"queries_timed_out\": " << timeout_count << ",\n";
    file << "    \"kg_vertices\": " << kg.getVerticesCount() << ",\n";
    file << "    \"kg_edges\": " << kg.getEdgesCount() << ",\n";
    file << "    \"kg_relations\": " << kg.getRelationsCount() << ",\n";
    file << "    \"kg_labels\": " << kg.getLabelsCount() << ",\n";
    file << "    \"embedding_dim\": " << kg.getVertexEmbeddingDim() << ",\n";
    file << "    \"ann_entry_point\": " << ann.getEntryPoint() << ",\n";
    file << "    \"ann_index_dir\": " << jsonString(ann_index_dir) << ",\n";
    file << "    \"mem_rss_peak_mb\": " << formatFloat(readVmPeakMb(), 2) << "\n";
    file << "  },\n";
    file << "  \"queries\": {\n";
    for (size_t i = 0; i < summaries.size(); ++i) {
        const ParentQuerySummary& query = summaries[i];
        file << "    \"" << jsonEscape(query.name) << "\": {\n";
        file << "      \"source\": " << jsonString(query.source_name) << ",\n";
        file << "      \"elapsed_ms\": " << formatMs(query.elapsed_ms) << ",\n";
        file << "      \"status\": " << jsonString(query.status) << ",\n";
        file << "      \"timed_out\": " << jsonBool(query.timed_out) << ",\n";
        file << "      \"exit_code\": " << query.exit_code << ",\n";
        file << "      \"signal\": " << query.signal_no << ",\n";
        file << "      \"result_file\": " << jsonString(query.name + ".txt") << ",\n";
        file << "      \"detail_timing_file\": "
             << jsonString(std::string("_query_timing/") + query.name + ".json") << "\n";
        file << "    }" << (i + 1 < summaries.size() ? "," : "") << "\n";
    }
    file << "  }\n";
    file << "}\n";
}
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " KG_DIR QUERIES_ROOT OUTPUT_ROOT [K=3] [N_LIST=4096]"
                  << " [UNDIRECTED=1] [START=-1] [END=-1] [DEBUG=0] [TIMEOUT_SEC=3600]"
                  << std::endl;
        return 2;
    }

    const std::string kg_path = argv[1];
    const std::string queries_root = argv[2];
    const std::string output_root = argv[3];
    ui top_k = 3;
    std::vector<ui> topn_list(1, 4096);
    bool undirected_mode = true;
    int start_id = -1;
    int end_id = -1;
    bool debug_mode = false;
    unsigned long timeout_sec = 3600;

    try {
        if (argc > 4) {
            top_k = static_cast<ui>(std::stoul(argv[4]));
        }
        if (argc > 5) {
            topn_list = parseUiList(argv[5]);
        }
        if (argc > 6) {
            undirected_mode = std::stoi(argv[6]) != 0;
        }
        if (argc > 7) {
            start_id = std::stoi(argv[7]);
        }
        if (argc > 8) {
            end_id = std::stoi(argv[8]);
        }
        if (argc > 9) {
            debug_mode = std::stoi(argv[9]) != 0;
        }
        if (argc > 10) {
            timeout_sec = std::stoul(argv[10]);
        }
    } catch (const std::exception& error) {
        std::cerr << "Invalid argument: " << error.what() << std::endl;
        return 2;
    }

    if (top_k == 0) {
        std::cerr << "K must be positive" << std::endl;
        return 2;
    }
    if (start_id >= 0 && end_id >= 0 && start_id > end_id) {
        std::cerr << "START must not exceed END" << std::endl;
        return 2;
    }
    if (!isDirectory(kg_path)) {
        std::cerr << "KG directory does not exist: " << kg_path << std::endl;
        return 1;
    }
    if (!isDirectory(queries_root)) {
        std::cerr << "Query root does not exist: " << queries_root << std::endl;
        return 1;
    }
    if (!ensureDirectory(output_root)) {
        std::cerr << "Cannot create output root: " << output_root << std::endl;
        return 1;
    }

    try {
        Stopwatch step;
        EmbeddingDiGraph kg(false, true);
        kg.loadFromKG(kg_path, true);
        const double kg_load_ms = step.elapsed_ms();

        step.reset();
        VamanaANN ann(debug_mode);
        VamanaANN::BuildConfig ann_config;
        ann_config.use_full_vamana_link = true;
        ann_config.max_degree = 32;
        ann_config.Lbuild = 128;
        ann_config.max_candidate_size = 750;
        ann_config.num_build_threads = 128;
        const std::string ann_index_dir = joinPath(kg_path, "index/node_vamana");
        ann.buildOrLoad(kg, ann_config, ann_index_dir);
        const double ann_load_ms = step.elapsed_ms();

        step.reset();
        std::unordered_map<std::string, ui> kg_rel_name_to_id;
        std::unordered_map<std::string, LabelID> kg_label_name_to_id;
        buildKgNameTables(kg, kg_rel_name_to_id, kg_label_name_to_id);
        const double vocab_ms = step.elapsed_ms();

        step.reset();
        const std::vector<QueryEntry> all_entries = collectQueryEntries(queries_root);
        const double scan_ms = step.elapsed_ms();
        if (all_entries.empty()) {
            std::cerr << "No new_graph query directories found under: " << queries_root << std::endl;
            return 1;
        }

        std::vector<QueryEntry> selected_entries;
        for (size_t i = 0; i < all_entries.size(); ++i) {
            if (start_id >= 0 && all_entries[i].id < start_id) {
                continue;
            }
            if (end_id >= 0 && all_entries[i].id > end_id) {
                continue;
            }
            selected_entries.push_back(all_entries[i]);
        }
        if (selected_entries.empty()) {
            std::cerr << "No queries remain after START and END filtering" << std::endl;
            return 1;
        }

        const std::vector<std::string> series_names = collectSeriesNames(selected_entries);
        std::cout << "kg=" << kg_path
                  << " queries=" << selected_entries.size()
                  << " series=" << series_names.size()
                  << " budgets=" << topn_list.size()
                  << " timeout_sec=" << timeout_sec << std::endl;

        for (size_t budget_index = 0; budget_index < topn_list.size(); ++budget_index) {
            const ui top_n = topn_list[budget_index];
            for (size_t series_index = 0; series_index < series_names.size(); ++series_index) {
                const std::string& series = series_names[series_index];
                const std::vector<QueryEntry> series_entries = entriesForSeries(selected_entries, series);
                const std::string output_series_root = seriesRoot(output_root, top_n, series);
                if (!ensureDirectory(output_series_root)) {
                    throw std::runtime_error("cannot create series output directory");
                }
                const std::string temp_dir = makeTempDirectory(output_series_root, top_k, top_n);
                if (!ensureDirectory(temp_dir)) {
                    throw std::runtime_error("cannot create temporary output directory");
                }
                const std::string timing_dir = joinPath(temp_dir, "_query_timing");
                if (!ensureDirectory(timing_dir)) {
                    throw std::runtime_error("cannot create query timing directory");
                }
                const std::string progress_path = joinPath(temp_dir, "progress.tsv");
                writeProgressHeader(progress_path);

                std::cout << "start top_n=" << top_n
                          << " series=" << series
                          << " queries=" << series_entries.size() << std::endl;
                Stopwatch series_stopwatch;
                std::vector<ParentQuerySummary> summaries;
                summaries.reserve(series_entries.size());

                for (size_t query_index = 0; query_index < series_entries.size(); ++query_index) {
                    const QueryEntry& entry = series_entries[query_index];
                    appendProgress(progress_path, "START", entry, NULL);
                    std::cout.flush();
                    std::cerr.flush();
                    const pid_t child = fork();
                    if (child == 0) {
                        int code = 0;
                        try {
                            code = runOneQueryChild(
                                entry,
                                temp_dir,
                                joinPath(timing_dir, entry.name + ".json"),
                                kg,
                                ann,
                                kg_rel_name_to_id,
                                kg_label_name_to_id,
                                top_k,
                                top_n,
                                undirected_mode,
                                debug_mode);
                        } catch (const std::exception& error) {
                            std::cerr << "Query failed: " << entry.source_name
                                      << ": " << error.what() << std::endl;
                            code = 3;
                        } catch (...) {
                            std::cerr << "Query failed: " << entry.source_name << std::endl;
                            code = 4;
                        }
                        std::cout.flush();
                        std::cerr.flush();
                        _exit(code);
                    }

                    ParentQuerySummary summary;
                    if (child < 0) {
                        summary.name = entry.name;
                        summary.series = entry.series;
                        summary.source_name = entry.source_name;
                        summary.id = entry.id;
                        summary.status = "FORK_FAIL";
                        summary.signal_no = -1;
                    } else {
                        summary = waitForChild(child, entry, timeout_sec);
                    }

                    if (isSuccessful(summary)) {
                        const std::string result_path = joinPath(temp_dir, entry.name + ".txt");
                        const std::string timing_path = joinPath(timing_dir, entry.name + ".json");
                        if (!isRegularFile(result_path) || !isRegularFile(timing_path)) {
                            summary.status = "FAILED";
                        }
                    }
                    if (!isSuccessful(summary)) {
                        ensureFailureArtifacts(temp_dir, timing_dir, entry, top_k, top_n,
                                               undirected_mode, timeout_sec, summary);
                    }
                    summaries.push_back(summary);
                    appendProgress(progress_path, summary.status, entry, &summaries.back());
                }

                const double series_ms = series_stopwatch.elapsed_ms();
                writeSeriesSummary(joinPath(temp_dir, "timing.json"),
                                   kg_path,
                                   queries_root,
                                   output_root,
                                   series,
                                   ann_index_dir,
                                   top_k,
                                   top_n,
                                   undirected_mode,
                                   start_id,
                                   end_id,
                                   debug_mode,
                                   timeout_sec,
                                   kg_load_ms,
                                   ann_load_ms,
                                   vocab_ms,
                                   scan_ms,
                                   series_ms,
                                   countEntriesForSeries(all_entries, series),
                                   summaries,
                                   kg,
                                   ann);

                const std::string final_dir = makeFinalDirectory(
                    output_series_root, top_k, top_n, undirected_mode, series_ms);
                std::error_code rename_error;
                std::filesystem::rename(std::filesystem::path(temp_dir),
                                        std::filesystem::path(final_dir),
                                        rename_error);
                if (rename_error) {
                    std::cerr << "Cannot finalize output directory: "
                              << rename_error.message() << " at " << temp_dir << std::endl;
                    return 1;
                }
                std::cout << final_dir << std::endl;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Fuse failed: " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
