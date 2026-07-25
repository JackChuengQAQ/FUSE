#include "newvamana.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <sstream>
#include <memory>
#include <mutex>
#include <vector>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string indexMetaPath(const std::string& index_dir) {
    return index_dir + "/meta";
}

std::string indexGraphPath(const std::string& index_dir) {
    return index_dir + "/graph";
}

void writeKvFile(const std::string& filename, const std::map<std::string, std::string>& kv) {
    std::ofstream out(filename.c_str());
    for (std::map<std::string, std::string>::const_iterator it = kv.begin(); it != kv.end(); ++it) {
        out << it->first << "=" << it->second << "\n";
    }
}

std::map<std::string, std::string> parseKvFile(const std::string& filename) {
    std::map<std::string, std::string> kv;
    std::ifstream in(filename.c_str());
    std::string line;
    while (std::getline(in, line)) {
        const size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        kv[line.substr(0, pos)] = line.substr(pos + 1);
    }
    return kv;
}

}  

namespace {
inline ui graphSlackCap(ui max_degree) {
    return static_cast<ui>(static_cast<float>(max_degree) * VamanaANN::kGraphSlackFactor);
}
}  

ui VamanaANN::computeNaiveLsearch(ui node_sim_topk) {
    return (node_sim_topk * 4 > 128u) ? node_sim_topk * 4 : 128u;
}

VamanaANN::VamanaANN(bool verbose) {
    graph_ = NULL;
    verbose_ = verbose;
    n_ = 0;
    dim_ = 0;
    entry_point_ = 0;
    alpha_ = 1.2f;
    max_degree_ = 32;
    max_candidate_size_ = 128;
}

float VamanaANN::l2Distance(const float* lhs, const float* rhs) const {
    float acc = 0.0f;
    for (ui i = 0; i < dim_; ++i) {
        float diff = lhs[i] - rhs[i];
        acc += diff * diff;
    }
    return acc;
}

VertexID VamanaANN::chooseEntryPoint(ui sample_size) const {
    if (n_ == 0) {
        return 0;
    }

    ui use_sample = std::min(sample_size, n_);
    if (use_sample == 0) {
        use_sample = 1;
    }

    std::vector<float> center(dim_, 0.0f);
    for (ui i = 0; i < use_sample; ++i) {
        const float* emb = graph_->getVertexEmbedding(i);
        for (ui d = 0; d < dim_; ++d) {
            center[d] += emb[d];
        }
    }

    float inv = 1.0f / static_cast<float>(use_sample);
    for (ui d = 0; d < dim_; ++d) {
        center[d] *= inv;
    }

    VertexID best = 0;
    float best_dist = std::numeric_limits<float>::max();
    for (ui i = 0; i < use_sample; ++i) {
        float cur = l2Distance(graph_->getVertexEmbedding(i), center.data());
        if (cur < best_dist) {
            best_dist = cur;
            best = i;
        }
    }
    return best;
}

VertexID VamanaANN::chooseMedoid() const {
    if (n_ == 0) {
        return 0;
    }

    std::vector<float> center(dim_, 0.0f);
    for (ui i = 0; i < n_; ++i) {
        const float* emb = graph_->getVertexEmbedding(i);
        for (ui d = 0; d < dim_; ++d) {
            center[d] += emb[d];
        }
    }

    const float inv = 1.0f / static_cast<float>(n_);
    for (ui d = 0; d < dim_; ++d) {
        center[d] *= inv;
    }

    VertexID medoid = 0;
    float best_dist = std::numeric_limits<float>::max();
    for (ui i = 0; i < n_; ++i) {
        float cur = l2Distance(center.data(), graph_->getVertexEmbedding(i));
        if (cur < best_dist || (cur == best_dist && i < medoid)) {
            best_dist = cur;
            medoid = i;
        }
    }
    return medoid;
}

std::vector<VertexID> VamanaANN::collectDirectedNeighbors(VertexID vertex) const {
    std::vector<VertexID> neighbors;

    ui out_count = 0;
    const VertexID* out_ptr = graph_->getOutNeighbors(vertex, out_count);
    ui in_count = 0;
    const VertexID* in_ptr = graph_->getInNeighbors(vertex, in_count);

    neighbors.reserve(out_count + in_count);
    for (ui i = 0; i < out_count; ++i) {
        neighbors.push_back(out_ptr[i]);
    }
    for (ui i = 0; i < in_count; ++i) {
        neighbors.push_back(in_ptr[i]);
    }

    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    return neighbors;
}

void VamanaANN::pruneNeighbors(VertexID center, std::vector<Candidate>& candidates, std::vector<VertexID>& output,
                               std::vector<float>& occlude) const {
    output.clear();
    if (candidates.empty()) {
        return;
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.distance != b.distance) {
            return a.distance < b.distance;
        }
        return a.id < b.id;
    });

    const size_t candidate_size =
        std::min(candidates.size(), static_cast<size_t>(max_candidate_size_));

    occlude.assign(candidate_size, 0.0f);
    float cur_alpha = 1.0f;

    while (cur_alpha <= alpha_ + 1e-6f && output.size() < max_degree_) {
        for (size_t i = 0; i < candidate_size && output.size() < max_degree_; ++i) {
            if (occlude[i] > cur_alpha) {
                continue;
            }

            occlude[i] = std::numeric_limits<float>::max();
            if (candidates[i].id == center) {
                continue;
            }

            output.push_back(candidates[i].id);

            const float* emb_i = graph_->getVertexEmbedding(candidates[i].id);
            for (size_t j = i + 1; j < candidate_size; ++j) {
                if (occlude[j] > alpha_) {
                    continue;
                }
                const float* emb_j = graph_->getVertexEmbedding(candidates[j].id);
                float dij = l2Distance(emb_i, emb_j);
                if (dij == 0.0f) {
                    occlude[j] = std::numeric_limits<float>::max();
                } else {
                    float value = candidates[j].distance / dij;
                    if (value > occlude[j]) {
                        occlude[j] = value;
                    }
                }
            }
        }
        cur_alpha *= 1.2f;
    }

    std::sort(output.begin(), output.end());
    output.erase(std::unique(output.begin(), output.end()), output.end());
    if (output.size() > max_degree_) {
        output.resize(max_degree_);
    }
}

void VamanaANN::trimVertexNeighbors(VertexID center, std::vector<float>& occlude_scratch) {
    std::vector<Candidate> candidates;
    candidates.reserve(graph_index_[center].size());

    const float* center_emb = graph_->getVertexEmbedding(center);
    for (size_t i = 0; i < graph_index_[center].size(); ++i) {
        VertexID v = graph_index_[center][i];
        candidates.push_back(Candidate{v, l2Distance(center_emb, graph_->getVertexEmbedding(v))});
    }

    std::vector<VertexID> pruned;
    pruneNeighbors(center, candidates, pruned, occlude_scratch);
    graph_index_[center].swap(pruned);
}

void VamanaANN::addReverseEdgesAndTrim(std::vector<float>& occlude_scratch) {
    for (ui src = 0; src < n_; ++src) {
        for (size_t i = 0; i < graph_index_[src].size(); ++i) {
            VertexID dst = graph_index_[src][i];
            if (dst >= n_ || dst == src) {
                continue;
            }
            graph_index_[dst].push_back(src);
        }
    }

    for (ui v = 0; v < n_; ++v) {
        std::vector<VertexID>& nbrs = graph_index_[v];
        std::sort(nbrs.begin(), nbrs.end());
        nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
        if (nbrs.size() > max_degree_) {
            trimVertexNeighbors(v, occlude_scratch);
        }
    }
}

void VamanaANN::buildFromKgNeighbors(std::vector<float>& occlude_scratch) {
    for (ui v = 0; v < n_; ++v) {
        std::vector<VertexID> raw = collectDirectedNeighbors(v);
        std::vector<Candidate> candidates;
        candidates.reserve(raw.size());

        const float* center_emb = graph_->getVertexEmbedding(v);
        for (size_t i = 0; i < raw.size(); ++i) {
            VertexID nb = raw[i];
            candidates.push_back(Candidate{nb, l2Distance(center_emb, graph_->getVertexEmbedding(nb))});
        }

        pruneNeighbors(v, candidates, graph_index_[v], occlude_scratch);
    }
    addReverseEdgesAndTrim(occlude_scratch);
}

ui VamanaANN::iterateToFixedPoint(const float* query, ui list_size, VamanaSearchScratch& scratch, bool record_expanded,
                                  VertexID target_id, std::mutex* neighbor_locks) const {
    VamanaSearchQueue& search_queue = scratch.search_queue;
    VamanaVisitedSet& visited_set = scratch.visited_set;
    std::vector<VamanaCandidate>& expanded_list = scratch.expanded_list;

    search_queue.clear();
    visited_set.clear();
    expanded_list.clear();

    const int32_t cap = static_cast<int32_t>(std::max<ui>(1, list_size));
    search_queue.reserve(cap);

    search_queue.insert(entry_point_, l2Distance(query, graph_->getVertexEmbedding(entry_point_)));
    ui num_cmps = 1;

    while (search_queue.hasUnexpandedNode()) {
        const VamanaCandidate& cur = search_queue.getClosestUnexpanded();
        if (record_expanded && target_id != cur.id) {
            expanded_list.push_back(cur);
        }

        std::vector<VertexID> neighbors;
        if (neighbor_locks != NULL) {
            std::lock_guard<std::mutex> lock(neighbor_locks[cur.id]);
            neighbors = graph_index_[cur.id];
        } else {
            neighbors = graph_index_[cur.id];
        }
        for (size_t i = 0; i < neighbors.size(); ++i) {
            VertexID neighbor = neighbors[i];
            if (neighbor >= n_ || visited_set.check(neighbor)) {
                continue;
            }
            visited_set.set(neighbor);
            float dist = l2Distance(query, graph_->getVertexEmbedding(neighbor));
            search_queue.insert(neighbor, dist);
            ++num_cmps;
        }
    }
    return num_cmps;
}

void VamanaANN::interInsert(VertexID src, const std::vector<VertexID>& src_neighbors, std::vector<float>& occlude_scratch,
                           std::mutex* neighbor_locks) {
    const ui slack_cap = graphSlackCap(max_degree_);

    for (size_t i = 0; i < src_neighbors.size(); ++i) {
        VertexID dst = src_neighbors[i];
        if (dst >= n_ || dst == src) {
            continue;
        }

        bool need_prune = false;
        std::vector<Candidate> candidates;

        if (neighbor_locks != NULL) {
            std::lock_guard<std::mutex> lock(neighbor_locks[dst]);
            std::vector<VertexID>& dst_neighbors = graph_index_[dst];
            if (std::find(dst_neighbors.begin(), dst_neighbors.end(), src) == dst_neighbors.end()) {
                if (dst_neighbors.size() < slack_cap) {
                    dst_neighbors.push_back(src);
                } else {
                    candidates.reserve(dst_neighbors.size() + 1);
                    for (size_t j = 0; j < dst_neighbors.size(); ++j) {
                        candidates.push_back(Candidate{dst_neighbors[j], 0.0f});
                    }
                    candidates.push_back(Candidate{src, 0.0f});
                    need_prune = true;
                }
            }
            if (need_prune) {
                const float* dst_emb = graph_->getVertexEmbedding(dst);
                for (size_t j = 0; j < candidates.size(); ++j) {
                    candidates[j].distance = l2Distance(dst_emb, graph_->getVertexEmbedding(candidates[j].id));
                }
                std::vector<VertexID> new_dst_neighbors;
                pruneNeighbors(dst, candidates, new_dst_neighbors, occlude_scratch);
                graph_index_[dst].swap(new_dst_neighbors);
            }
        } else {
            std::vector<VertexID>& dst_neighbors = graph_index_[dst];
            if (std::find(dst_neighbors.begin(), dst_neighbors.end(), src) == dst_neighbors.end()) {
                if (dst_neighbors.size() < slack_cap) {
                    dst_neighbors.push_back(src);
                } else {
                    candidates.reserve(dst_neighbors.size() + 1);
                    for (size_t j = 0; j < dst_neighbors.size(); ++j) {
                        candidates.push_back(Candidate{dst_neighbors[j], 0.0f});
                    }
                    candidates.push_back(Candidate{src, 0.0f});
                    need_prune = true;
                }
            }
            if (need_prune) {
                const float* dst_emb = graph_->getVertexEmbedding(dst);
                for (size_t j = 0; j < candidates.size(); ++j) {
                    candidates[j].distance = l2Distance(dst_emb, graph_->getVertexEmbedding(candidates[j].id));
                }
                std::vector<VertexID> new_dst_neighbors;
                pruneNeighbors(dst, candidates, new_dst_neighbors, occlude_scratch);
                graph_index_[dst].swap(new_dst_neighbors);
            }
        }
    }
}

void VamanaANN::link(ui Lbuild, ui num_threads) {
    const bool use_parallel =
#if defined(_OPENMP)
        num_threads > 1;
#else
        false;
#endif

#if defined(_OPENMP)
    if (use_parallel) {
        omp_set_num_threads(static_cast<int>(num_threads));
    }
#endif

    std::unique_ptr<std::mutex[]> neighbor_locks;
    if (use_parallel) {
        neighbor_locks.reset(new std::mutex[n_]);
    }

    const uint64_t progress_step = std::max<uint64_t>(
        1, (static_cast<uint64_t>(n_) + 99) / 100);
    std::atomic<uint64_t> completed(0);
    std::mutex progress_log_mutex;
    uint64_t last_reported = 0;
    const std::chrono::steady_clock::time_point progress_start = std::chrono::steady_clock::now();

    std::cerr << "[VamanaANN] link start n=" << n_
              << " Lbuild=" << Lbuild
              << " threads=" << (use_parallel ? num_threads : 1)
              << " scratch=per-thread" << std::endl;

    auto report_progress = [&]() {
        const uint64_t done = completed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (done != static_cast<uint64_t>(n_) && done % progress_step != 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(progress_log_mutex);
        if (done <= last_reported) {
            return;
        }
        last_reported = done;

        const double elapsed_sec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - progress_start).count();
        const double rate = elapsed_sec > 0.0 ? static_cast<double>(done) / elapsed_sec : 0.0;
        const double eta_sec = rate > 0.0
            ? static_cast<double>(static_cast<uint64_t>(n_) - done) / rate
            : 0.0;

        std::cerr << std::fixed << std::setprecision(2)
                  << "[VamanaANN] link progress "
                  << (100.0 * static_cast<double>(done) / static_cast<double>(n_)) << "%"
                  << " (" << done << "/" << n_ << ")"
                  << " elapsed=" << elapsed_sec << "s"
                  << " rate=" << rate << " vertices/s"
                  << " eta=" << eta_sec << "s"
                  << std::endl;
    };

    auto link_one = [&](ui id, VamanaSearchScratch& scratch, std::vector<float>& occlude_scratch) {
        const float* query = graph_->getVertexEmbedding(id);
        iterateToFixedPoint(query, Lbuild, scratch, true, id, use_parallel ? neighbor_locks.get() : NULL);

        std::vector<Candidate> prune_input;
        prune_input.reserve(scratch.expanded_list.size());
        for (size_t i = 0; i < scratch.expanded_list.size(); ++i) {
            prune_input.push_back(Candidate{scratch.expanded_list[i].id, scratch.expanded_list[i].distance});
        }

        std::vector<VertexID> pruned_list;
        pruneNeighbors(id, prune_input, pruned_list, occlude_scratch);

        if (use_parallel) {
            {
                std::lock_guard<std::mutex> lock(neighbor_locks[id]);
                graph_index_[id] = pruned_list;
            }
            interInsert(id, graph_index_[id], occlude_scratch, neighbor_locks.get());
        } else {
            graph_index_[id] = pruned_list;
            interInsert(id, graph_index_[id], occlude_scratch, NULL);
        }
    };

#if defined(_OPENMP)
    if (use_parallel) {
        #pragma omp parallel
        {
            VamanaSearchScratch scratch(n_, static_cast<int32_t>(Lbuild));
            std::vector<float> occlude_scratch;

            #pragma omp for schedule(dynamic, 1)
            for (int id = 0; id < static_cast<int>(n_); ++id) {
                link_one(static_cast<ui>(id), scratch, occlude_scratch);
                report_progress();
            }
        }
    } else
#endif
    {
        VamanaSearchScratch scratch(n_, static_cast<int32_t>(Lbuild));
        std::vector<float> occlude_scratch;
        for (ui id = 0; id < n_; ++id) {
            link_one(id, scratch, occlude_scratch);
            report_progress();
        }
    }

    std::cerr << "[VamanaANN] final degree trim start" << std::endl;

    auto final_trim = [&](ui id, std::vector<float>& occlude_scratch) {
        if (graph_index_[id].size() <= max_degree_) {
            return;
        }
        std::vector<Candidate> candidates;
        const float* center_emb = graph_->getVertexEmbedding(id);
        for (size_t j = 0; j < graph_index_[id].size(); ++j) {
            VertexID nb = graph_index_[id][j];
            candidates.push_back(Candidate{nb, l2Distance(center_emb, graph_->getVertexEmbedding(nb))});
        }
        std::vector<VertexID> new_neighbors;
        pruneNeighbors(id, candidates, new_neighbors, occlude_scratch);
        graph_index_[id].swap(new_neighbors);
    };

#if defined(_OPENMP)
    if (use_parallel) {
        #pragma omp parallel for schedule(dynamic, 1)
        for (int id = 0; id < static_cast<int>(n_); ++id) {
            std::vector<float> occlude_scratch;
            final_trim(static_cast<ui>(id), occlude_scratch);
        }
    } else
#endif
    {
        std::vector<float> occlude_scratch;
        for (ui id = 0; id < n_; ++id) {
            final_trim(id, occlude_scratch);
        }
    }
    std::cerr << "[VamanaANN] final degree trim complete" << std::endl;
}

void VamanaANN::build(const EmbeddingDiGraph& graph, const BuildConfig& config) {
    graph_ = &graph;
    n_ = graph.getVerticesCount();
    dim_ = graph.getVertexEmbeddingDim();

    if (n_ == 0 || dim_ == 0) {
        std::cerr << "[VamanaANN] invalid graph or embedding dimension." << std::endl;
        graph_index_.clear();
        entry_point_ = 0;
        return;
    }

    max_degree_ = std::max<ui>(1, config.max_degree);
    max_candidate_size_ = std::max(max_degree_, config.max_candidate_size);
    alpha_ = static_cast<float>(config.alpha_scaled_by_1000) / 1000.0f;
    if (alpha_ < 1.0f) {
        alpha_ = 1.0f;
    }

    graph_index_.assign(n_, std::vector<VertexID>());
    std::vector<float> occlude_scratch;

    if (config.use_full_vamana_link) {
        const ui Lbuild = std::max<ui>(max_degree_, config.Lbuild);
        const ui threads = std::max<ui>(1, config.num_build_threads);
        entry_point_ = chooseMedoid();
        if (verbose_) {
            std::cout << "[VamanaANN] building (full Vamana link), n=" << n_ << " Lbuild=" << Lbuild
                      << " max_degree=" << max_degree_ << " alpha=" << alpha_ << " entry=" << entry_point_
                      << " threads=" << threads << std::endl;
        }
        link(Lbuild, threads);
    } else {
        entry_point_ = chooseEntryPoint(config.sample_size_for_entry);
        if (verbose_) {
            std::cout << "[VamanaANN] building (KG-neighbor prune), n=" << n_ << " max_degree=" << max_degree_
                      << std::endl;
        }
        buildFromKgNeighbors(occlude_scratch);
    }

    if (verbose_) {
        size_t total_edges = 0;
        for (ui v = 0; v < n_; ++v) {
            total_edges += graph_index_[v].size();
        }
        float avg = n_ == 0 ? 0.0f : static_cast<float>(total_edges) / static_cast<float>(n_);
        std::cout << "[VamanaANN] build finished, vertices=" << n_ << ", entry=" << entry_point_
                  << ", avg_degree=" << avg << std::endl;
    }
}

void VamanaANN::collectTopKFromQueue(const VamanaSearchScratch& scratch, ui top_k, std::vector<SearchResult>& out) {
    out.clear();
    const int32_t qsize = scratch.search_queue.size();
    const int32_t take = static_cast<int32_t>(std::min<ui>(top_k, static_cast<ui>(qsize)));
    out.reserve(static_cast<size_t>(take));
    for (int32_t k = 0; k < take; ++k) {
        VamanaCandidate c = scratch.search_queue[k];
        out.push_back(SearchResult{c.id, c.distance});
    }
}

std::vector<VamanaANN::SearchResult> VamanaANN::searchBruteForce(const float* query_embedding, ui top_k) const {
    struct MaxCmp {
        bool operator()(const Candidate& lhs, const Candidate& rhs) const {
            if (lhs.distance != rhs.distance) {
                return lhs.distance < rhs.distance;
            }
            return lhs.id < rhs.id;
        }
    };
    std::priority_queue<Candidate, std::vector<Candidate>, MaxCmp> best;
    for (VertexID v = 0; v < n_; ++v) {
        float dist = l2Distance(query_embedding, graph_->getVertexEmbedding(v));
        if (best.size() < top_k) {
            best.push(Candidate{v, dist});
        } else if (dist < best.top().distance || (dist == best.top().distance && v < best.top().id)) {
            best.pop();
            best.push(Candidate{v, dist});
        }
    }

    std::vector<SearchResult> results;
    results.reserve(best.size());
    while (!best.empty()) {
        Candidate c = best.top();
        best.pop();
        results.push_back(SearchResult{c.id, c.distance});
    }
    std::sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
        if (a.distance != b.distance) {
            return a.distance < b.distance;
        }
        return a.id < b.id;
    });
    return results;
}

std::vector<VamanaANN::SearchResult> VamanaANN::searchOnGraph(const float* query_embedding, ui top_k, ui Lsearch) const {
    VamanaSearchScratch scratch(n_, static_cast<int32_t>(Lsearch));
    iterateToFixedPoint(query_embedding, Lsearch, scratch, false, n_, NULL);

    std::vector<SearchResult> results;
    collectTopKFromQueue(scratch, top_k, results);
    return results;
}

std::vector<VamanaANN::SearchResult> VamanaANN::search(const float* query_embedding, ui top_k,
                                                       const SearchConfig& config) const {
    std::vector<SearchResult> empty;
    if (graph_ == NULL || query_embedding == NULL || n_ == 0 || top_k == 0) {
        return empty;
    }

    if (!config.use_graph_search) {
        return searchBruteForce(query_embedding, top_k);
    }
    const ui Lsearch = std::max<ui>(top_k, std::max<ui>(1, config.ef_search));
    return searchOnGraph(query_embedding, top_k, Lsearch);
}

std::vector<VamanaANN::SearchResult> VamanaANN::searchByVertex(VertexID query_vertex, ui top_k,
                                                               const SearchConfig& config) const {
    if (graph_ == NULL || query_vertex >= n_) {
        return std::vector<SearchResult>();
    }
    return search(graph_->getVertexEmbedding(query_vertex), top_k, config);
}

bool VamanaANN::indexExists(const std::string& index_dir) {
    return fs::exists(indexMetaPath(index_dir)) && fs::exists(indexGraphPath(index_dir));
}

void VamanaANN::save(const std::string& index_dir) const {
    fs::create_directories(index_dir);

    std::map<std::string, std::string> meta;
    meta["num_points"] = std::to_string(n_);
    meta["dim"] = std::to_string(dim_);
    meta["entry_point"] = std::to_string(entry_point_);
    meta["max_degree"] = std::to_string(max_degree_);
    meta["max_candidate_size"] = std::to_string(max_candidate_size_);
    meta["alpha_scaled_by_1000"] = std::to_string(static_cast<int>(alpha_ * 1000.0f + 0.5f));
    writeKvFile(indexMetaPath(index_dir), meta);

    std::ofstream graph_out(indexGraphPath(index_dir).c_str());
    for (ui i = 0; i < n_; ++i) {
        graph_out << i << " ";
        for (size_t j = 0; j < graph_index_[i].size(); ++j) {
            graph_out << graph_index_[i][j] << " ";
        }
        graph_out << "\n";
    }
    if (verbose_) {
        std::cout << "[VamanaANN] index saved to " << index_dir << std::endl;
    }
}

bool VamanaANN::load(const EmbeddingDiGraph& graph, const std::string& index_dir) {
    graph_ = &graph;
    n_ = graph.getVerticesCount();
    dim_ = graph.getVertexEmbeddingDim();
    if (n_ == 0 || dim_ == 0) {
        return false;
    }

    const std::map<std::string, std::string> meta = parseKvFile(indexMetaPath(index_dir));
    if (meta.count("num_points") == 0 || meta.count("dim") == 0 || meta.count("entry_point") == 0) {
        return false;
    }

    const ui meta_n = static_cast<ui>(std::stoul(meta.at("num_points")));
    const ui meta_dim = static_cast<ui>(std::stoul(meta.at("dim")));
    if (meta_n != n_ || meta_dim != dim_) {
        std::cerr << "[VamanaANN] index/KG mismatch: index n=" << meta_n << " dim=" << meta_dim << " but KG n=" << n_
                  << " dim=" << dim_ << std::endl;
        return false;
    }

    entry_point_ = static_cast<VertexID>(std::stoul(meta.at("entry_point")));
    max_degree_ = meta.count("max_degree") ? static_cast<ui>(std::stoul(meta.at("max_degree"))) : 32u;
    max_candidate_size_ =
        meta.count("max_candidate_size") ? static_cast<ui>(std::stoul(meta.at("max_candidate_size"))) : 750u;
    if (meta.count("alpha_scaled_by_1000")) {
        alpha_ = static_cast<float>(std::stoi(meta.at("alpha_scaled_by_1000"))) / 1000.0f;
    } else {
        alpha_ = 1.2f;
    }

    graph_index_.assign(n_, std::vector<VertexID>());
    std::ifstream graph_in(indexGraphPath(index_dir).c_str());
    if (!graph_in.good()) {
        return false;
    }

    std::string line;
    while (std::getline(graph_in, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        ui id = 0;
        iss >> id;
        if (id >= n_) {
            continue;
        }
        VertexID neighbor = 0;
        while (iss >> neighbor) {
            if (neighbor < n_) {
                graph_index_[id].push_back(neighbor);
            }
        }
    }

    if (verbose_) {
        std::cout << "[VamanaANN] index loaded from " << index_dir << " (entry=" << entry_point_ << ")" << std::endl;
    }
    return true;
}

bool VamanaANN::buildOrLoad(const EmbeddingDiGraph& graph, const BuildConfig& config, const std::string& index_dir) {
    if (!config.use_full_vamana_link) {
        build(graph, config);
        return true;
    }

    if (indexExists(index_dir) && load(graph, index_dir)) {
        return true;
    }

    if (verbose_) {
        std::cout << "[VamanaANN] building index (full Vamana link) ..." << std::endl;
    }
    build(graph, config);
    save(index_dir);
    return true;
}
