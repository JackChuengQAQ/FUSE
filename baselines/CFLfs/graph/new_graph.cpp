#include "new_graph.h"
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace {

constexpr size_t kStreamReadBufferBytes = 8 * 1024 * 1024;

inline std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) {
        return file;
    }
    char last = dir.back();
    if (last == '/' || last == '\\') {
        return dir + file;
    }
    return dir + "/" + file;
}

inline bool fileExists(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode) || S_ISLNK(st.st_mode);
}

inline bool directoryExists(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

std::vector<std::string> listShardFiles(const std::string& dir, const std::string& expected_extension) {
    std::vector<std::pair<long long, std::string> > digit_named;
    std::vector<std::pair<std::string, std::string> > other_named;

    DIR* dp = ::opendir(dir.c_str());
    if (dp == NULL) {
        std::cerr << "Cannot open directory " << dir << " : " << std::strerror(errno) << std::endl;
        std::exit(-1);
    }

    struct dirent* entry = NULL;
    while ((entry = ::readdir(dp)) != NULL) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }
        if (!expected_extension.empty()) {
            if (name.size() < expected_extension.size()) {
                continue;
            }
            if (name.compare(name.size() - expected_extension.size(), expected_extension.size(), expected_extension) != 0) {
                continue;
            }
        }

        std::string stem = expected_extension.empty()
                                ? name
                                : name.substr(0, name.size() - expected_extension.size());

        bool all_digit = !stem.empty();
        for (size_t i = 0; i < stem.size(); ++i) {
            if (stem[i] < '0' || stem[i] > '9') {
                all_digit = false;
                break;
            }
        }
        if (all_digit) {
            digit_named.emplace_back(std::strtoll(stem.c_str(), NULL, 10), joinPath(dir, name));
        }
        else {
            other_named.emplace_back(name, joinPath(dir, name));
        }
    }
    ::closedir(dp);

    std::sort(digit_named.begin(), digit_named.end(),
              [](const std::pair<long long, std::string>& a, const std::pair<long long, std::string>& b) {
                  return a.first < b.first;
              });
    std::sort(other_named.begin(), other_named.end(),
              [](const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) {
                  return a.first < b.first;
              });

    std::vector<std::string> result;
    result.reserve(digit_named.size() + other_named.size());
    for (size_t i = 0; i < digit_named.size(); ++i) {
        result.push_back(digit_named[i].second);
    }
    for (size_t i = 0; i < other_named.size(); ++i) {
        result.push_back(other_named[i].second);
    }
    return result;
}

class BufferedFileReader {
public:
    explicit BufferedFileReader(const std::string& path) : path_(path) {
        file_ = std::fopen(path.c_str(), "rb");
        if (file_ == NULL) {
            std::cerr << "Cannot open file " << path << " : " << std::strerror(errno) << std::endl;
            std::exit(-1);
        }
        buffer_ = new char[kStreamReadBufferBytes];
        std::setvbuf(file_, buffer_, _IOFBF, kStreamReadBufferBytes);
    }

    ~BufferedFileReader() {
        if (file_ != NULL) {
            std::fclose(file_);
            file_ = NULL;
        }
        delete[] buffer_;
        buffer_ = NULL;
    }

    BufferedFileReader(const BufferedFileReader&) = delete;
    BufferedFileReader& operator=(const BufferedFileReader&) = delete;

    FILE* file() const {
        return file_;
    }

    const std::string& path() const {
        return path_;
    }

    void readExact(void* dst, size_t bytes, const char* what) {
        if (bytes == 0) {
            return;
        }
        size_t got = std::fread(dst, 1, bytes, file_);
        if (got != bytes) {
            std::cerr << "Unexpected end of file while reading " << what << " from " << path_ << std::endl;
            std::exit(-1);
        }
    }

    uint32_t readU32(const char* what) {
        uint32_t value = 0;
        readExact(&value, sizeof(value), what);
        return value;
    }

    uint64_t readU64(const char* what) {
        uint64_t value = 0;
        readExact(&value, sizeof(value), what);
        return value;
    }

    void readString(std::string& out, const char* what) {
        uint32_t len = readU32(what);
        out.resize(len);
        if (len != 0) {
            readExact(&out[0], len, what);
        }
    }

private:
    std::string path_;
    FILE* file_ = NULL;
    char* buffer_ = NULL;
};

inline bool splitTwoTabs(const char* line, size_t length, const char*& part1_begin, size_t& part1_size,
                         const char*& part2_begin, size_t& part2_size) {
    const char* tab1 = static_cast<const char*>(std::memchr(line, '\t', length));
    if (tab1 == NULL) {
        return false;
    }
    part1_begin = line;
    part1_size = static_cast<size_t>(tab1 - line);
    part2_begin = tab1 + 1;
    part2_size = length - part1_size - 1;
    return true;
}

inline bool splitThreeTabs(const char* line, size_t length, const char*& a_begin, size_t& a_size,
                           const char*& b_begin, size_t& b_size, const char*& c_begin, size_t& c_size) {
    const char* tab1 = static_cast<const char*>(std::memchr(line, '\t', length));
    if (tab1 == NULL) {
        return false;
    }
    size_t remaining = length - (static_cast<size_t>(tab1 - line) + 1);
    const char* tab2 = static_cast<const char*>(std::memchr(tab1 + 1, '\t', remaining));
    if (tab2 == NULL) {
        return false;
    }
    a_begin = line;
    a_size = static_cast<size_t>(tab1 - line);
    b_begin = tab1 + 1;
    b_size = static_cast<size_t>(tab2 - b_begin);
    c_begin = tab2 + 1;
    c_size = length - a_size - 1 - b_size - 1;
    return true;
}

void readLineStripCR(FILE* fp, std::string& line, bool& eof) {
    line.clear();
    int ch = std::fgetc(fp);
    if (ch == EOF) {
        eof = true;
        return;
    }
    while (ch != EOF && ch != '\n') {
        line.push_back(static_cast<char>(ch));
        ch = std::fgetc(fp);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    eof = false;
}

}

EmbeddingDiGraph::EmbeddingDiGraph(bool verbose, bool enable_edge_index) {
    verbose_ = verbose;
    enable_edge_index_ = enable_edge_index;

    vertices_count_ = 0;
    edges_count_ = 0;
    relations_count_ = 0;
    labels_count_ = 0;

    max_out_degree_ = 0;
    max_in_degree_ = 0;
    max_total_degree_ = 0;
    max_label_frequency_ = 0;

    out_offsets_ = NULL;
    out_neighbors_ = NULL;
    out_edge_ids_ = NULL;

    in_offsets_ = NULL;
    in_neighbors_ = NULL;
    in_edge_ids_ = NULL;

    edge_src_ = NULL;
    edge_dst_ = NULL;
    edge_relations_ = NULL;

    vertex_label_offsets_ = NULL;
    vertex_labels_ = NULL;

    vertex_embeddings_ = NULL;
    edge_embeddings_ = NULL;
    label_embeddings_ = NULL;
    vertex_embedding_dim_ = 0;
    edge_embedding_dim_ = 0;
    label_embedding_dim_ = 0;

    node_name_table_ = NULL;
    relation_name_table_ = NULL;
    type_name_table_ = NULL;
    edge_index_ = NULL;
}

EmbeddingDiGraph::~EmbeddingDiGraph() {
    releaseGraph();
}

void EmbeddingDiGraph::releaseGraph() {
    delete[] out_offsets_;
    delete[] out_neighbors_;
    delete[] out_edge_ids_;

    delete[] in_offsets_;
    delete[] in_neighbors_;
    delete[] in_edge_ids_;

    delete[] edge_src_;
    delete[] edge_dst_;
    delete[] edge_relations_;

    delete[] vertex_label_offsets_;
    delete[] vertex_labels_;

    delete[] vertex_embeddings_;
    delete[] edge_embeddings_;
    delete[] label_embeddings_;

    delete[] node_name_table_;
    delete[] relation_name_table_;
    delete[] type_name_table_;

    if (edge_index_ != NULL) {
        for (auto& kv : *edge_index_) {
            delete kv.second;
        }
        delete edge_index_;
    }

    out_offsets_ = NULL;
    out_neighbors_ = NULL;
    out_edge_ids_ = NULL;

    in_offsets_ = NULL;
    in_neighbors_ = NULL;
    in_edge_ids_ = NULL;

    edge_src_ = NULL;
    edge_dst_ = NULL;
    edge_relations_ = NULL;

    vertex_label_offsets_ = NULL;
    vertex_labels_ = NULL;

    vertex_embeddings_ = NULL;
    edge_embeddings_ = NULL;
    label_embeddings_ = NULL;

    node_name_table_ = NULL;
    relation_name_table_ = NULL;
    type_name_table_ = NULL;
    edge_index_ = NULL;

    vertices_count_ = 0;
    relations_count_ = 0;
    labels_count_ = 0;
    edges_count_ = 0;

    max_out_degree_ = 0;
    max_in_degree_ = 0;
    max_total_degree_ = 0;
    max_label_frequency_ = 0;

    vertex_embedding_dim_ = 0;
    edge_embedding_dim_ = 0;
    label_embedding_dim_ = 0;
}

void EmbeddingDiGraph::sortCSRByNeighborAndRelation(ui* offsets, VertexID* neighbors, ui* edge_ids, ui max_degree) {
    if (max_degree <= 1) {
        return;
    }

    std::pair<VertexID, ui>* buffer = new std::pair<VertexID, ui>[max_degree];

    for (ui i = 0; i < vertices_count_; ++i) {
        ui begin = offsets[i];
        ui end = offsets[i + 1];
        ui degree = end - begin;
        if (degree <= 1) {
            continue;
        }

        for (ui j = 0; j < degree; ++j) {
            buffer[j].first = neighbors[begin + j];
            buffer[j].second = edge_ids[begin + j];
        }

        std::sort(buffer, buffer + degree,
                  [this](const std::pair<VertexID, ui>& lhs, const std::pair<VertexID, ui>& rhs) -> bool {
                      if (lhs.first != rhs.first) {
                          return lhs.first < rhs.first;
                      }
                      ui lhs_rel = edge_relations_[lhs.second];
                      ui rhs_rel = edge_relations_[rhs.second];
                      if (lhs_rel != rhs_rel) {
                          return lhs_rel < rhs_rel;
                      }
                      return lhs.second < rhs.second;
                  });

        for (ui j = 0; j < degree; ++j) {
            neighbors[begin + j] = buffer[j].first;
            edge_ids[begin + j] = buffer[j].second;
        }
    }

    delete[] buffer;
}

namespace {

struct NameToIdEntry {
    std::string name;
    ui id;
};

void loadNameToIdLines(const std::string& path, std::vector<NameToIdEntry>& out) {
    BufferedFileReader reader(path);
    std::string line;
    line.reserve(64);
    while (true) {
        bool eof = false;
        readLineStripCR(reader.file(), line, eof);
        if (eof && line.empty()) {
            break;
        }
        if (line.empty() && eof) {
            break;
        }
        if (line.empty()) {
            continue;
        }
        const char* tab = static_cast<const char*>(std::memchr(line.data(), '\t', line.size()));
        if (tab == NULL) {
            std::cerr << "Malformed name_to_id line in " << path << " : '" << line << "'" << std::endl;
            std::exit(-1);
        }
        size_t name_len = static_cast<size_t>(tab - line.data());
        const char* id_str = tab + 1;
        size_t id_len = line.size() - name_len - 1;

        char* end_ptr = NULL;
        unsigned long parsed = std::strtoul(std::string(id_str, id_len).c_str(), &end_ptr, 10);
        if (end_ptr == NULL || *end_ptr != '\0') {
            std::cerr << "Invalid id in name_to_id line of " << path << " : '" << line << "'" << std::endl;
            std::exit(-1);
        }

        NameToIdEntry entry;
        entry.name.assign(line.data(), name_len);
        entry.id = static_cast<ui>(parsed);
        out.push_back(std::move(entry));

        if (eof) {
            break;
        }
    }
}

struct EmbedBinHeader {
    uint64_t count;
    uint32_t dim;
};

EmbedBinHeader loadEmbedBinHeader(BufferedFileReader& reader) {
    char magic[8];
    reader.readExact(magic, 8, "EMBEDBIN magic");
    static const char expected[8] = {'E', 'M', 'B', 'E', 'D', 'B', 'I', 'N'};
    if (std::memcmp(magic, expected, 8) != 0) {
        std::cerr << "Invalid EMBEDBIN magic in " << reader.path() << std::endl;
        std::exit(-1);
    }
    EmbedBinHeader header{};
    header.count = reader.readU64("EMBEDBIN count");
    header.dim = reader.readU32("EMBEDBIN dim");
    return header;
}

ui parseStatsValue(const std::string& path, const char* key) {
    BufferedFileReader reader(path);
    std::string line;
    line.reserve(64);
    const size_t key_len = std::strlen(key);
    while (true) {
        bool eof = false;
        readLineStripCR(reader.file(), line, eof);
        if (line.empty() && eof) {
            break;
        }
        if (line.size() > key_len && line.compare(0, key_len, key) == 0 && line[key_len] == '=') {
            char* end_ptr = NULL;
            unsigned long parsed = std::strtoul(line.c_str() + key_len + 1, &end_ptr, 10);
            if (end_ptr == line.c_str() + key_len + 1) {
                std::cerr << "Invalid stats value in " << path << " : '" << line << "'" << std::endl;
                std::exit(-1);
            }
            if (parsed > std::numeric_limits<ui>::max()) {
                std::cerr << "Stats value overflow in " << path << " : '" << line << "'" << std::endl;
                std::exit(-1);
            }
            return static_cast<ui>(parsed);
        }
        if (eof) {
            break;
        }
    }
    std::cerr << "Missing stats key '" << key << "' in " << path << std::endl;
    std::exit(-1);
}

std::vector<std::string> loadTextLines(const std::string& path) {
    BufferedFileReader reader(path);
    std::vector<std::string> lines;
    std::string line;
    line.reserve(96);
    while (true) {
        bool eof = false;
        readLineStripCR(reader.file(), line, eof);
        if (line.empty() && eof) {
            break;
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (eof) {
            break;
        }
    }
    return lines;
}

void loadCompactEmbeddingBin(const std::string& path, ui expected_count, float*& out_emb, ui& out_dim) {
    BufferedFileReader reader(path);
    uint32_t count = reader.readU32("compact embedding count");
    uint32_t dim = reader.readU32("compact embedding dim");
    if (count != expected_count) {
        std::cerr << path << " count " << count << " != expected " << expected_count << std::endl;
        std::exit(-1);
    }
    if (dim == 0 || dim > std::numeric_limits<ui>::max()) {
        std::cerr << "Invalid compact embedding dim in " << path << ": " << dim << std::endl;
        std::exit(-1);
    }
    out_dim = static_cast<ui>(dim);
    size_t total_floats = static_cast<size_t>(count) * out_dim;
    out_emb = total_floats == 0 ? NULL : new float[total_floats];
    if (out_emb != NULL) {
        reader.readExact(out_emb, total_floats * sizeof(float), "compact embedding vectors");
    }
}

void buildCSRFromEdgeTriplets(ui vertices_count,
                              ui relations_count,
                              const std::vector<VertexID>& edge_src,
                              const std::vector<VertexID>& edge_dst,
                              const std::vector<ui>& edge_rel,
                              ui& edges_count_out,
                              ui*& out_offsets,
                              VertexID*& out_neighbors,
                              ui*& out_edge_ids,
                              ui*& in_offsets,
                              VertexID*& in_neighbors,
                              ui*& in_edge_ids,
                              VertexID*& edge_src_out,
                              VertexID*& edge_dst_out,
                              ui*& edge_relations_out,
                              ui& max_out_degree_out,
                              ui& max_in_degree_out,
                              ui& max_total_degree_out) {
    const ui edges_count = static_cast<ui>(edge_src.size());
    edges_count_out = edges_count;

    ui* out_degrees = new ui[vertices_count]();
    ui* in_degrees = new ui[vertices_count]();
    for (ui e = 0; e < edges_count; ++e) {
        if (edge_src[e] >= vertices_count || edge_dst[e] >= vertices_count) {
            std::cerr << "Edge endpoint out of range at edge " << e << std::endl;
            std::exit(-1);
        }
        if (edge_rel[e] >= relations_count) {
            std::cerr << "Edge relation out of range at edge " << e << std::endl;
            std::exit(-1);
        }
        out_degrees[edge_src[e]] += 1;
        in_degrees[edge_dst[e]] += 1;
    }

    out_offsets = new ui[vertices_count + 1];
    in_offsets = new ui[vertices_count + 1];
    out_offsets[0] = 0;
    in_offsets[0] = 0;
    max_out_degree_out = 0;
    max_in_degree_out = 0;
    max_total_degree_out = 0;
    for (ui i = 1; i <= vertices_count; ++i) {
        ui out_deg = out_degrees[i - 1];
        ui in_deg = in_degrees[i - 1];
        out_offsets[i] = out_offsets[i - 1] + out_deg;
        in_offsets[i] = in_offsets[i - 1] + in_deg;
        if (out_deg > max_out_degree_out) {
            max_out_degree_out = out_deg;
        }
        if (in_deg > max_in_degree_out) {
            max_in_degree_out = in_deg;
        }
        ui total_deg = out_deg + in_deg;
        if (total_deg > max_total_degree_out) {
            max_total_degree_out = total_deg;
        }
    }

    edge_src_out = new VertexID[edges_count];
    edge_dst_out = new VertexID[edges_count];
    edge_relations_out = new ui[edges_count];
    out_neighbors = new VertexID[edges_count];
    out_edge_ids = new ui[edges_count];
    in_neighbors = new VertexID[edges_count];
    in_edge_ids = new ui[edges_count];

    ui* out_cursor = new ui[vertices_count];
    ui* in_cursor = new ui[vertices_count];
    std::memcpy(out_cursor, out_offsets, sizeof(ui) * vertices_count);
    std::memcpy(in_cursor, in_offsets, sizeof(ui) * vertices_count);

    for (ui edge_id = 0; edge_id < edges_count; ++edge_id) {
        VertexID src = edge_src[edge_id];
        VertexID dst = edge_dst[edge_id];
        ui rel = edge_rel[edge_id];
        edge_src_out[edge_id] = src;
        edge_dst_out[edge_id] = dst;
        edge_relations_out[edge_id] = rel;

        ui out_pos = out_cursor[src]++;
        out_neighbors[out_pos] = dst;
        out_edge_ids[out_pos] = edge_id;

        ui in_pos = in_cursor[dst]++;
        in_neighbors[in_pos] = src;
        in_edge_ids[in_pos] = edge_id;
    }

    delete[] out_cursor;
    delete[] in_cursor;
    delete[] out_degrees;
    delete[] in_degrees;
}

}

void EmbeddingDiGraph::loadFromTextBinary(const std::string& folder_path, bool skip_vertex_type_files) {
    releaseGraph();
    const std::string pickled_graph = joinPath(folder_path, "FactKG.graph.txt");
    const std::string compact_edges = joinPath(folder_path, "edges.txt");
    if (fileExists(pickled_graph)) {
        loadFromPickledKGFolder(folder_path, skip_vertex_type_files);
        return;
    }
    if (fileExists(compact_edges)) {
        loadFromCompactKGFolder(folder_path, skip_vertex_type_files);
        return;
    }
    std::cerr << "Unknown KG folder format: " << folder_path
              << " (need FactKG.graph.txt or edges.txt)" << std::endl;
    std::exit(-1);
}

void EmbeddingDiGraph::loadFromPickledKGFolder(const std::string& folder_path, bool skip_vertex_type_files) {
    auto start_time = std::chrono::high_resolution_clock::now();

    const std::string relation_name_path = joinPath(folder_path, "relation_name_to_id.txt");
    const std::string type_name_path = joinPath(folder_path, "type_name_to_id.txt");
    const std::string node_name_dir = joinPath(folder_path, "node_name_to_id");
    const std::string type_to_nodes_path = joinPath(folder_path, "FactKG.type_to_nodes.txt");
    const std::string graph_path = joinPath(folder_path, "FactKG.graph.txt");
    const std::string node_embedding_dir = joinPath(folder_path, "node_embeddings");
    const std::string relation_embedding_path = joinPath(folder_path, "relation_embeddings.bin");
    const std::string type_embedding_path = joinPath(folder_path, "type_embeddings.bin");

    std::vector<NameToIdEntry> relation_entries;
    loadNameToIdLines(relation_name_path, relation_entries);
    if (relation_entries.empty()) {
        std::cerr << "relation_name_to_id.txt is empty." << std::endl;
        std::exit(-1);
    }
    ui relations_max = 0;
    for (size_t i = 0; i < relation_entries.size(); ++i) {
        if (relation_entries[i].id > relations_max) {
            relations_max = relation_entries[i].id;
        }
    }
    relations_count_ = relations_max + 1;
    if (relations_count_ != relation_entries.size()) {
        std::cerr << "relation_name_to_id.txt id range is non-contiguous: max="
                  << relations_max << " count=" << relation_entries.size() << std::endl;
        std::exit(-1);
    }
    relation_name_table_ = new std::string[relations_count_];
    sparse_hash_map<std::string, ui> relation_name_to_id;
    relation_name_to_id.reserve(relations_count_ * 2);
    for (size_t i = 0; i < relation_entries.size(); ++i) {
        ui id = relation_entries[i].id;
        if (id >= relations_count_) {
            std::cerr << "Relation id out of range: " << id << std::endl;
            std::exit(-1);
        }
        relation_name_table_[id] = relation_entries[i].name;
        relation_name_to_id[relation_entries[i].name] = id;
    }
    std::vector<NameToIdEntry>().swap(relation_entries);

    sparse_hash_map<std::string, ui> type_name_to_id;
    if (skip_vertex_type_files) {
        labels_count_ = 0;
        type_name_table_ = NULL;
        if (verbose_) {
            std::cout << "[loader] skip vertex type files (|L|=0)" << std::endl;
        }
    } else {
        std::vector<NameToIdEntry> type_entries;
        loadNameToIdLines(type_name_path, type_entries);
        if (type_entries.empty()) {
            labels_count_ = 0;
            type_name_table_ = NULL;
            if (verbose_) {
                std::cout << "[loader] type_name_to_id.txt is empty; |L|=0 (labels ignored)"
                          << std::endl;
            }
        } else {
            ui labels_max = 0;
            for (size_t i = 0; i < type_entries.size(); ++i) {
                if (type_entries[i].id > labels_max) {
                    labels_max = type_entries[i].id;
                }
            }
            labels_count_ = labels_max + 1;
            if (labels_count_ != type_entries.size()) {
                std::cerr << "type_name_to_id.txt id range is non-contiguous: max="
                          << labels_max << " count=" << type_entries.size() << std::endl;
                std::exit(-1);
            }
            type_name_table_ = new std::string[labels_count_];
            type_name_to_id.reserve(labels_count_ * 2);
            for (size_t i = 0; i < type_entries.size(); ++i) {
                ui id = type_entries[i].id;
                if (id >= labels_count_) {
                    std::cerr << "Type id out of range: " << id << std::endl;
                    std::exit(-1);
                }
                type_name_table_[id] = type_entries[i].name;
                type_name_to_id[type_entries[i].name] = id;
            }
            std::vector<NameToIdEntry>().swap(type_entries);
        }
    }

    if (!directoryExists(node_name_dir)) {
        std::cerr << "Missing directory " << node_name_dir << std::endl;
        std::exit(-1);
    }
    std::vector<std::string> node_name_shards = listShardFiles(node_name_dir, ".txt");
    if (node_name_shards.empty()) {
        std::cerr << "No node_name_to_id shards found in " << node_name_dir << std::endl;
        std::exit(-1);
    }

    std::vector<std::pair<ui, std::string> > all_node_entries;
    {
        size_t total_lines = 0;
        for (size_t s = 0; s < node_name_shards.size(); ++s) {
            BufferedFileReader probe(node_name_shards[s]);
            char buf[1 << 16];
            size_t got = 0;
            while ((got = std::fread(buf, 1, sizeof(buf), probe.file())) > 0) {
                for (size_t i = 0; i < got; ++i) {
                    if (buf[i] == '\n') {
                        ++total_lines;
                    }
                }
            }
        }
        all_node_entries.reserve(total_lines);
    }

    {
        std::string line;
        line.reserve(96);
        for (size_t s = 0; s < node_name_shards.size(); ++s) {
            BufferedFileReader reader(node_name_shards[s]);
            while (true) {
                bool eof = false;
                readLineStripCR(reader.file(), line, eof);
                if (line.empty() && eof) {
                    break;
                }
                if (line.empty()) {
                    if (eof) break; else continue;
                }
                const char* tab = static_cast<const char*>(std::memchr(line.data(), '\t', line.size()));
                if (tab == NULL) {
                    std::cerr << "Malformed node_name_to_id line in " << node_name_shards[s] << std::endl;
                    std::exit(-1);
                }
                size_t name_len = static_cast<size_t>(tab - line.data());
                const char* id_str = tab + 1;
                size_t id_len = line.size() - name_len - 1;
                char* end_ptr = NULL;
                std::string id_buf(id_str, id_len);
                unsigned long parsed = std::strtoul(id_buf.c_str(), &end_ptr, 10);
                if (end_ptr == NULL || *end_ptr != '\0') {
                    std::cerr << "Invalid id in node_name_to_id line of " << node_name_shards[s] << std::endl;
                    std::exit(-1);
                }
                all_node_entries.push_back(std::make_pair(static_cast<ui>(parsed), std::string(line.data(), name_len)));
                if (eof) {
                    break;
                }
            }
        }
    }

    if (all_node_entries.empty()) {
        std::cerr << "node_name_to_id is empty." << std::endl;
        std::exit(-1);
    }

    ui nodes_max = 0;
    for (size_t i = 0; i < all_node_entries.size(); ++i) {
        if (all_node_entries[i].first > nodes_max) {
            nodes_max = all_node_entries[i].first;
        }
    }
    vertices_count_ = nodes_max + 1;
    if (vertices_count_ != all_node_entries.size()) {
        std::cerr << "node_name_to_id id range is non-contiguous: max="
                  << nodes_max << " count=" << all_node_entries.size() << std::endl;
        std::exit(-1);
    }

    node_name_table_ = new std::string[vertices_count_];
    sparse_hash_map<std::string, VertexID> node_name_to_id;
    node_name_to_id.reserve(static_cast<size_t>(vertices_count_) * 2);
    for (size_t i = 0; i < all_node_entries.size(); ++i) {
        ui id = all_node_entries[i].first;
        if (id >= vertices_count_) {
            std::cerr << "Node id out of range: " << id << std::endl;
            std::exit(-1);
        }
        node_name_table_[id] = all_node_entries[i].second;
        node_name_to_id[all_node_entries[i].second] = static_cast<VertexID>(id);
    }
    std::vector<std::pair<ui, std::string> >().swap(all_node_entries);

    if (verbose_) {
        std::cout << "[loader] meta loaded: |V|=" << vertices_count_
                  << " |R|=" << relations_count_
                  << " |L|=" << labels_count_ << std::endl;
    }

    std::vector<std::vector<LabelID> > vertex_labels_tmp(vertices_count_);
    if (labels_count_ > 0) {
        BufferedFileReader reader(type_to_nodes_path);
        std::string line;
        line.reserve(128);
        while (true) {
            bool eof = false;
            readLineStripCR(reader.file(), line, eof);
            if (line.empty() && eof) {
                break;
            }
            if (line.empty()) {
                if (eof) break; else continue;
            }
            const char* a_begin = NULL;
            const char* b_begin = NULL;
            size_t a_size = 0;
            size_t b_size = 0;
            if (!splitTwoTabs(line.data(), line.size(), a_begin, a_size, b_begin, b_size)) {
                std::cerr << "Malformed FactKG.type_to_nodes.txt line: '" << line << "'" << std::endl;
                std::exit(-1);
            }
            std::string type_name(a_begin, a_size);
            std::string node_name(b_begin, b_size);

            auto type_it = type_name_to_id.find(type_name);
            if (type_it == type_name_to_id.end()) {
                std::cerr << "Type missing from type_name_to_id: " << type_name << std::endl;
                std::exit(-1);
            }
            auto node_it = node_name_to_id.find(node_name);
            if (node_it == node_name_to_id.end()) {
                if (eof) break;
                continue;
            }
            vertex_labels_tmp[node_it->second].push_back(static_cast<LabelID>(type_it->second));
            if (eof) {
                break;
            }
        }
    }

    sparse_hash_map<std::string, ui>().swap(type_name_to_id);

    ui total_vertex_labels = 0;
    std::vector<ui> label_frequency(labels_count_, 0);
    for (ui v = 0; v < vertices_count_; ++v) {
        std::vector<LabelID>& labels = vertex_labels_tmp[v];
        if (!labels.empty()) {
            std::sort(labels.begin(), labels.end());
            labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
            total_vertex_labels += static_cast<ui>(labels.size());
            for (size_t k = 0; k < labels.size(); ++k) {
                label_frequency[labels[k]] += 1;
            }
        }
    }
    max_label_frequency_ = 0;
    for (size_t i = 0; i < label_frequency.size(); ++i) {
        if (label_frequency[i] > max_label_frequency_) {
            max_label_frequency_ = label_frequency[i];
        }
    }

    vertex_label_offsets_ = new ui[vertices_count_ + 1];
    vertex_labels_ = total_vertex_labels == 0 ? NULL : new LabelID[total_vertex_labels];
    vertex_label_offsets_[0] = 0;
    ui cursor = 0;
    for (ui v = 0; v < vertices_count_; ++v) {
        const std::vector<LabelID>& labels = vertex_labels_tmp[v];
        for (size_t k = 0; k < labels.size(); ++k) {
            vertex_labels_[cursor++] = labels[k];
        }
        vertex_label_offsets_[v + 1] = cursor;
    }
    std::vector<std::vector<LabelID> >().swap(vertex_labels_tmp);
    std::vector<ui>().swap(label_frequency);

    if (verbose_) {
        std::cout << "[loader] vertex labels loaded: total=" << total_vertex_labels
                  << " max_freq=" << max_label_frequency_ << std::endl;
    }

    ui* out_degrees = new ui[vertices_count_]();
    ui* in_degrees = new ui[vertices_count_]();
    uint64_t edge_total = 0;

    {
        BufferedFileReader reader(graph_path);
        std::string line;
        line.reserve(160);
        while (true) {
            bool eof = false;
            readLineStripCR(reader.file(), line, eof);
            if (line.empty() && eof) {
                break;
            }
            if (line.empty()) {
                if (eof) break; else continue;
            }
            const char* a_begin = NULL;
            const char* b_begin = NULL;
            const char* c_begin = NULL;
            size_t a_size = 0, b_size = 0, c_size = 0;
            if (!splitThreeTabs(line.data(), line.size(), a_begin, a_size, b_begin, b_size, c_begin, c_size)) {
                std::cerr << "Malformed FactKG.graph.txt line: '" << line << "'" << std::endl;
                std::exit(-1);
            }
            std::string src_name(a_begin, a_size);
            std::string dst_name(c_begin, c_size);
            auto src_it = node_name_to_id.find(src_name);
            if (src_it == node_name_to_id.end()) {
                std::cerr << "Graph source missing from node_name_to_id: " << src_name << std::endl;
                std::exit(-1);
            }
            auto dst_it = node_name_to_id.find(dst_name);
            if (dst_it == node_name_to_id.end()) {
                std::cerr << "Graph target missing from node_name_to_id: " << dst_name << std::endl;
                std::exit(-1);
            }
            VertexID src = src_it->second;
            VertexID dst = dst_it->second;
            out_degrees[src] += 1;
            in_degrees[dst] += 1;
            ++edge_total;
            if (eof) {
                break;
            }
        }
    }

    if (edge_total > std::numeric_limits<ui>::max()) {
        std::cerr << "FactKG.graph.txt has too many edges for ui-sized arrays: " << edge_total << std::endl;
        std::exit(-1);
    }
    edges_count_ = static_cast<ui>(edge_total);

    out_offsets_ = new ui[vertices_count_ + 1];
    in_offsets_ = new ui[vertices_count_ + 1];
    out_offsets_[0] = 0;
    in_offsets_[0] = 0;
    max_out_degree_ = 0;
    max_in_degree_ = 0;
    max_total_degree_ = 0;
    for (ui i = 1; i <= vertices_count_; ++i) {
        ui out_deg = out_degrees[i - 1];
        ui in_deg = in_degrees[i - 1];
        out_offsets_[i] = out_offsets_[i - 1] + out_deg;
        in_offsets_[i] = in_offsets_[i - 1] + in_deg;
        if (out_deg > max_out_degree_) max_out_degree_ = out_deg;
        if (in_deg > max_in_degree_) max_in_degree_ = in_deg;
        ui total_deg = out_deg + in_deg;
        if (total_deg > max_total_degree_) max_total_degree_ = total_deg;
    }

    edge_src_ = new VertexID[edges_count_];
    edge_dst_ = new VertexID[edges_count_];
    edge_relations_ = new ui[edges_count_];
    out_neighbors_ = new VertexID[edges_count_];
    out_edge_ids_ = new ui[edges_count_];
    in_neighbors_ = new VertexID[edges_count_];
    in_edge_ids_ = new ui[edges_count_];

    ui* out_cursor = new ui[vertices_count_];
    ui* in_cursor = new ui[vertices_count_];
    std::memcpy(out_cursor, out_offsets_, sizeof(ui) * vertices_count_);
    std::memcpy(in_cursor, in_offsets_, sizeof(ui) * vertices_count_);

    {
        BufferedFileReader reader(graph_path);
        std::string line;
        line.reserve(160);
        ui edge_id = 0;
        while (true) {
            bool eof = false;
            readLineStripCR(reader.file(), line, eof);
            if (line.empty() && eof) {
                break;
            }
            if (line.empty()) {
                if (eof) break; else continue;
            }
            const char* a_begin = NULL;
            const char* b_begin = NULL;
            const char* c_begin = NULL;
            size_t a_size = 0, b_size = 0, c_size = 0;
            if (!splitThreeTabs(line.data(), line.size(), a_begin, a_size, b_begin, b_size, c_begin, c_size)) {
                std::cerr << "Malformed FactKG.graph.txt line: '" << line << "'" << std::endl;
                std::exit(-1);
            }
            std::string src_name(a_begin, a_size);
            std::string rel_name(b_begin, b_size);
            std::string dst_name(c_begin, c_size);
            auto src_it = node_name_to_id.find(src_name);
            auto dst_it = node_name_to_id.find(dst_name);
            auto rel_it = relation_name_to_id.find(rel_name);
            if (src_it == node_name_to_id.end() || dst_it == node_name_to_id.end()) {
                std::cerr << "Graph endpoint missing from node_name_to_id during second pass." << std::endl;
                std::exit(-1);
            }
            if (rel_it == relation_name_to_id.end()) {
                std::cerr << "Graph relation missing from relation_name_to_id: " << rel_name << std::endl;
                std::exit(-1);
            }
            if (edge_id >= edges_count_) {
                std::cerr << "FactKG.graph.txt grew between passes." << std::endl;
                std::exit(-1);
            }

            VertexID src = src_it->second;
            VertexID dst = dst_it->second;
            ui rel = rel_it->second;
            edge_src_[edge_id] = src;
            edge_dst_[edge_id] = dst;
            edge_relations_[edge_id] = rel;

            ui out_pos = out_cursor[src]++;
            out_neighbors_[out_pos] = dst;
            out_edge_ids_[out_pos] = edge_id;

            ui in_pos = in_cursor[dst]++;
            in_neighbors_[in_pos] = src;
            in_edge_ids_[in_pos] = edge_id;

            ++edge_id;
            if (eof) {
                break;
            }
        }
        if (edge_id != edges_count_) {
            std::cerr << "Edge count mismatch between passes: expected " << edges_count_
                      << " got " << edge_id << std::endl;
            std::exit(-1);
        }
    }

    delete[] out_cursor;
    delete[] in_cursor;
    delete[] out_degrees;
    delete[] in_degrees;

    sparse_hash_map<std::string, VertexID>().swap(node_name_to_id);
    sparse_hash_map<std::string, ui>().swap(relation_name_to_id);

    sortCSRByNeighborAndRelation(out_offsets_, out_neighbors_, out_edge_ids_, max_out_degree_);
    sortCSRByNeighborAndRelation(in_offsets_, in_neighbors_, in_edge_ids_, max_in_degree_);

    if (verbose_) {
        std::cout << "[loader] graph edges loaded: |E|=" << edges_count_ << std::endl;
    }

    {
        if (!directoryExists(node_embedding_dir)) {
            std::cerr << "Missing directory " << node_embedding_dir << std::endl;
            std::exit(-1);
        }
        std::vector<std::string> node_embed_shards = listShardFiles(node_embedding_dir, ".bin");
        if (node_embed_shards.empty()) {
            std::cerr << "No node_embeddings shards found." << std::endl;
            std::exit(-1);
        }

        uint64_t total_count = 0;
        uint64_t shared_dim = 0;
        std::vector<std::pair<uint64_t, uint64_t> > headers;
        headers.reserve(node_embed_shards.size());
        for (size_t s = 0; s < node_embed_shards.size(); ++s) {
            BufferedFileReader probe(node_embed_shards[s]);
            EmbedBinHeader header = loadEmbedBinHeader(probe);
            uint64_t cnt = header.count;
            uint64_t dim = static_cast<uint64_t>(header.dim);
            if (s == 0) {
                shared_dim = dim;
            }
            else if (dim != shared_dim) {
                std::cerr << "Inconsistent node embedding dim across shards: " << dim
                          << " vs " << shared_dim << " in " << node_embed_shards[s] << std::endl;
                std::exit(-1);
            }
            total_count += cnt;
            headers.push_back(std::make_pair(cnt, dim));
        }

        if (total_count != static_cast<uint64_t>(vertices_count_)) {
            std::cerr << "node_embeddings record count " << total_count
                      << " != vertices_count " << vertices_count_ << std::endl;
            std::exit(-1);
        }
        if (shared_dim == 0 || shared_dim > std::numeric_limits<ui>::max()) {
            std::cerr << "Invalid node embedding dim: " << shared_dim << std::endl;
            std::exit(-1);
        }
        vertex_embedding_dim_ = static_cast<ui>(shared_dim);

        size_t total_floats = static_cast<size_t>(vertices_count_) * vertex_embedding_dim_;
        vertex_embeddings_ = total_floats == 0 ? NULL : new float[total_floats];

        uint64_t global_idx = 0;
        std::string name_buf;
        name_buf.reserve(64);
        for (size_t s = 0; s < node_embed_shards.size(); ++s) {
            BufferedFileReader reader(node_embed_shards[s]);
            EmbedBinHeader header = loadEmbedBinHeader(reader);
            uint64_t cnt = header.count;
            uint64_t dim = static_cast<uint64_t>(header.dim);
            for (uint64_t i = 0; i < cnt; ++i) {
                uint32_t record_id = reader.readU32("node embedding id");
                if (record_id != static_cast<uint32_t>(global_idx)) {
                    std::cerr << "node_embeddings id mismatch at global idx " << global_idx
                              << ": record has id " << record_id
                              << " in " << node_embed_shards[s] << std::endl;
                    std::exit(-1);
                }
                reader.readString(name_buf, "node embedding name");
                if (global_idx >= vertices_count_) {
                    std::cerr << "node_embeddings overflow at idx " << global_idx << std::endl;
                    std::exit(-1);
                }
                if (name_buf != node_name_table_[global_idx]) {
                    std::cerr << "node_embeddings name mismatch at id " << global_idx
                              << ": '" << name_buf << "' vs '" << node_name_table_[global_idx]
                              << "' in " << node_embed_shards[s] << std::endl;
                    std::exit(-1);
                }
                reader.readExact(vertex_embeddings_ + (size_t)global_idx * vertex_embedding_dim_,
                                 (size_t)vertex_embedding_dim_ * sizeof(float),
                                 "node embedding vector");
                ++global_idx;
            }
        }
        if (global_idx != static_cast<uint64_t>(vertices_count_)) {
            std::cerr << "node_embeddings final count mismatch." << std::endl;
            std::exit(-1);
        }

        if (verbose_) {
            std::cout << "[loader] node embeddings loaded: dim=" << vertex_embedding_dim_ << std::endl;
        }
    }

    if (fileExists(relation_embedding_path)) {
        BufferedFileReader reader(relation_embedding_path);
        EmbedBinHeader header = loadEmbedBinHeader(reader);
        uint64_t cnt = header.count;
        uint64_t dim = static_cast<uint64_t>(header.dim);
        if (cnt != static_cast<uint64_t>(relations_count_)) {
            std::cerr << "relation_embeddings record count " << cnt
                      << " != relations_count " << relations_count_ << std::endl;
            std::exit(-1);
        }
        if (dim == 0 || dim > std::numeric_limits<ui>::max()) {
            std::cerr << "Invalid relation embedding dim: " << dim << std::endl;
            std::exit(-1);
        }
        edge_embedding_dim_ = static_cast<ui>(dim);
        size_t total_floats = static_cast<size_t>(relations_count_) * edge_embedding_dim_;
        edge_embeddings_ = total_floats == 0 ? NULL : new float[total_floats];

        std::string name_buf;
        name_buf.reserve(64);
        for (uint64_t i = 0; i < cnt; ++i) {
            uint32_t record_id = reader.readU32("relation embedding id");
            if (record_id != static_cast<uint32_t>(i)) {
                std::cerr << "relation_embeddings id mismatch at index " << i
                          << ": record has id " << record_id << std::endl;
                std::exit(-1);
            }
            reader.readString(name_buf, "relation embedding name");
            if (name_buf != relation_name_table_[i]) {
                std::cerr << "relation_embeddings name mismatch at id " << i
                          << ": '" << name_buf << "' vs '" << relation_name_table_[i] << "'" << std::endl;
                std::exit(-1);
            }
            reader.readExact(edge_embeddings_ + (size_t)i * edge_embedding_dim_,
                             (size_t)edge_embedding_dim_ * sizeof(float),
                             "relation embedding vector");
        }

        if (verbose_) {
            std::cout << "[loader] relation embeddings loaded: dim=" << edge_embedding_dim_ << std::endl;
        }
    } else if (verbose_) {
        std::cout << "[loader] relation_embeddings.bin not found, skip relation embeddings." << std::endl;
    }

    if (fileExists(type_embedding_path) && labels_count_ > 0) {
        BufferedFileReader reader(type_embedding_path);
        EmbedBinHeader header = loadEmbedBinHeader(reader);
        uint64_t cnt = header.count;
        uint64_t dim = static_cast<uint64_t>(header.dim);
        if (cnt != static_cast<uint64_t>(labels_count_)) {
            std::cerr << "type_embeddings record count " << cnt
                      << " != labels_count " << labels_count_ << std::endl;
            std::exit(-1);
        }
        if (dim == 0 || dim > std::numeric_limits<ui>::max()) {
            std::cerr << "Invalid type embedding dim: " << dim << std::endl;
            std::exit(-1);
        }
        label_embedding_dim_ = static_cast<ui>(dim);
        size_t total_floats = static_cast<size_t>(labels_count_) * label_embedding_dim_;
        label_embeddings_ = total_floats == 0 ? NULL : new float[total_floats];

        std::string name_buf;
        name_buf.reserve(64);
        for (uint64_t i = 0; i < cnt; ++i) {
            uint32_t record_id = reader.readU32("type embedding id");
            if (record_id != static_cast<uint32_t>(i)) {
                std::cerr << "type_embeddings id mismatch at index " << i
                          << ": record has id " << record_id << std::endl;
                std::exit(-1);
            }
            reader.readString(name_buf, "type embedding name");
            if (name_buf != type_name_table_[i]) {
                std::cerr << "type_embeddings name mismatch at id " << i
                          << ": '" << name_buf << "' vs '" << type_name_table_[i] << "'" << std::endl;
                std::exit(-1);
            }
            reader.readExact(label_embeddings_ + (size_t)i * label_embedding_dim_,
                             (size_t)label_embedding_dim_ * sizeof(float),
                             "type embedding vector");
        }

        if (verbose_) {
            std::cout << "[loader] label embeddings loaded: dim=" << label_embedding_dim_ << std::endl;
        }
    }
    else if (verbose_) {
        std::cout << "[loader] type_embeddings.bin not found, skip label embeddings." << std::endl;
    }

    if (enable_edge_index_) {
        buildEdgeIndex();
    }

    if (verbose_) {
        auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time).count();
        std::cout << "KG load finished in " << cost << " ms." << std::endl;
    }
}

void EmbeddingDiGraph::loadFromCompactKGFolder(const std::string& folder_path, bool skip_vertex_type_files) {
    auto start_time = std::chrono::high_resolution_clock::now();

    const std::string stats_path = joinPath(folder_path, "stats.txt");
    const std::string node_names_path = joinPath(folder_path, "node_names.txt");
    const std::string relation_names_path = joinPath(folder_path, "relation_names.txt");
    const std::string edges_path = joinPath(folder_path, "edges.txt");
    const std::string node_embedding_path = joinPath(folder_path, "node_embeddings.bin");
    const std::string relation_embedding_path = joinPath(folder_path, "relation_embeddings.bin");

    vertices_count_ = parseStatsValue(stats_path, "node_num");
    relations_count_ = parseStatsValue(stats_path, "relation_num");
    const ui expected_edges = parseStatsValue(stats_path, "edge_num");

    std::vector<std::string> node_names = loadTextLines(node_names_path);
    std::vector<std::string> relation_names = loadTextLines(relation_names_path);
    if (node_names.size() != static_cast<size_t>(vertices_count_)) {
        std::cerr << "node_names.txt count " << node_names.size()
                  << " != stats node_num " << vertices_count_ << std::endl;
        std::exit(-1);
    }
    if (relation_names.size() != static_cast<size_t>(relations_count_)) {
        std::cerr << "relation_names.txt count " << relation_names.size()
                  << " != stats relation_num " << relations_count_ << std::endl;
        std::exit(-1);
    }

    node_name_table_ = new std::string[vertices_count_];
    for (ui i = 0; i < vertices_count_; ++i) {
        node_name_table_[i] = node_names[i];
    }
    relation_name_table_ = new std::string[relations_count_];
    for (ui i = 0; i < relations_count_; ++i) {
        relation_name_table_[i] = relation_names[i];
    }

    if (skip_vertex_type_files) {
        labels_count_ = 0;
        type_name_table_ = NULL;
        if (verbose_) {
            std::cout << "[loader] compact KG: skip vertex type files (|L|=0)" << std::endl;
        }
    } else {
        labels_count_ = 1;
        type_name_table_ = new std::string[1];
        type_name_table_[0] = "Entity";
    }
    vertex_label_offsets_ = new ui[vertices_count_ + 1];
    if (labels_count_ == 0) {
        vertex_labels_ = NULL;
        for (ui v = 0; v <= vertices_count_; ++v) {
            vertex_label_offsets_[v] = 0;
        }
        max_label_frequency_ = 0;
    } else {
        vertex_labels_ = new LabelID[vertices_count_];
        for (ui v = 0; v < vertices_count_; ++v) {
            vertex_label_offsets_[v] = v;
            vertex_labels_[v] = 0;
        }
        vertex_label_offsets_[vertices_count_] = vertices_count_;
        max_label_frequency_ = vertices_count_;
    }

    std::vector<VertexID> edge_src_tmp;
    std::vector<VertexID> edge_dst_tmp;
    std::vector<ui> edge_rel_tmp;
    edge_src_tmp.reserve(expected_edges);
    edge_dst_tmp.reserve(expected_edges);
    edge_rel_tmp.reserve(expected_edges);

    {
        BufferedFileReader reader(edges_path);
        std::string line;
        line.reserve(64);
        while (true) {
            bool eof = false;
            readLineStripCR(reader.file(), line, eof);
            if (line.empty() && eof) {
                break;
            }
            if (line.empty()) {
                if (eof) {
                    break;
                }
                continue;
            }
            const char* a_begin = NULL;
            const char* b_begin = NULL;
            const char* c_begin = NULL;
            size_t a_size = 0;
            size_t b_size = 0;
            size_t c_size = 0;
            if (!splitThreeTabs(line.data(), line.size(), a_begin, a_size, b_begin, b_size, c_begin, c_size)) {
                std::cerr << "Malformed edges.txt line: '" << line << "'" << std::endl;
                std::exit(-1);
            }
            char* end_ptr = NULL;
            unsigned long src_ul = std::strtoul(std::string(a_begin, a_size).c_str(), &end_ptr, 10);
            if (end_ptr == std::string(a_begin, a_size).c_str()) {
                std::cerr << "Invalid src in edges.txt: '" << line << "'" << std::endl;
                std::exit(-1);
            }
            end_ptr = NULL;
            unsigned long rel_ul = std::strtoul(std::string(b_begin, b_size).c_str(), &end_ptr, 10);
            if (end_ptr == std::string(b_begin, b_size).c_str()) {
                std::cerr << "Invalid relation in edges.txt: '" << line << "'" << std::endl;
                std::exit(-1);
            }
            end_ptr = NULL;
            unsigned long dst_ul = std::strtoul(std::string(c_begin, c_size).c_str(), &end_ptr, 10);
            if (end_ptr == std::string(c_begin, c_size).c_str()) {
                std::cerr << "Invalid dst in edges.txt: '" << line << "'" << std::endl;
                std::exit(-1);
            }
            edge_src_tmp.push_back(static_cast<VertexID>(src_ul));
            edge_rel_tmp.push_back(static_cast<ui>(rel_ul));
            edge_dst_tmp.push_back(static_cast<VertexID>(dst_ul));
            if (eof) {
                break;
            }
        }
    }

    if (edge_src_tmp.size() != static_cast<size_t>(expected_edges)) {
        std::cerr << "edges.txt count " << edge_src_tmp.size()
                  << " != stats edge_num " << expected_edges << std::endl;
        std::exit(-1);
    }

    buildCSRFromEdgeTriplets(vertices_count_, relations_count_, edge_src_tmp, edge_dst_tmp, edge_rel_tmp,
                             edges_count_, out_offsets_, out_neighbors_, out_edge_ids_, in_offsets_,
                             in_neighbors_, in_edge_ids_, edge_src_, edge_dst_, edge_relations_,
                             max_out_degree_, max_in_degree_, max_total_degree_);

    sortCSRByNeighborAndRelation(out_offsets_, out_neighbors_, out_edge_ids_, max_out_degree_);
    sortCSRByNeighborAndRelation(in_offsets_, in_neighbors_, in_edge_ids_, max_in_degree_);

    if (fileExists(node_embedding_path)) {
        loadCompactEmbeddingBin(node_embedding_path, vertices_count_, vertex_embeddings_, vertex_embedding_dim_);
    }
    if (fileExists(relation_embedding_path)) {
        loadCompactEmbeddingBin(relation_embedding_path, relations_count_, edge_embeddings_, edge_embedding_dim_);
    }

    if (enable_edge_index_) {
        buildEdgeIndex();
    }

    if (verbose_) {
        std::cout << "[loader] compact KG: |V|=" << vertices_count_
                  << " |R|=" << relations_count_
                  << " |E|=" << edges_count_
                  << " vertex_dim=" << vertex_embedding_dim_
                  << " relation_dim=" << edge_embedding_dim_ << std::endl;
        auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time).count();
        std::cout << "Compact KG load finished in " << cost << " ms." << std::endl;
    }
}

void EmbeddingDiGraph::loadFromKG(const std::string& folder_path, bool skip_vertex_type_files) {
    loadFromTextBinary(folder_path, skip_vertex_type_files);
}

void EmbeddingDiGraph::printMetaData() const {
    std::cout << "|V|: " << vertices_count_
              << ", |R|: " << relations_count_
              << ", |L|: " << labels_count_
              << ", |E|: " << edges_count_
              << " (directed)" << std::endl;

    std::cout << "Max Out Degree: " << max_out_degree_
              << ", Max In Degree: " << max_in_degree_
              << ", Max Total Degree: " << max_total_degree_
              << ", Max Label Frequency: " << max_label_frequency_ << std::endl;

    std::cout << "Vertex Embedding Dim: " << vertex_embedding_dim_
              << ", Edge Embedding Dim: " << edge_embedding_dim_
              << ", Label Embedding Dim: " << label_embedding_dim_ << std::endl;
}

void EmbeddingDiGraph::buildEdgeIndex() {
    if (edge_index_ != NULL) {
        for (auto& kv : *edge_index_) {
            delete kv.second;
        }
        delete edge_index_;
    }

    edge_index_ = new sparse_hash_map<uint64_t, std::vector<ui>* >();

    for (ui edge_id = 0; edge_id < edges_count_; ++edge_id) {
        uint64_t key = ((uint64_t)edge_src_[edge_id] << 32) | (uint64_t)edge_dst_[edge_id];

        auto iter = edge_index_->find(key);
        if (iter == edge_index_->end()) {
            (*edge_index_)[key] = new std::vector<ui>();
        }

        (*edge_index_)[key]->emplace_back(edge_id);
    }
}

ui EmbeddingDiGraph::getEdgeId(const VertexID src, const VertexID dst, const ui relation_id) const {
    const ui invalid_edge_id = std::numeric_limits<ui>::max();
    if (src >= vertices_count_ || dst >= vertices_count_ || relation_id >= relations_count_) {
        return invalid_edge_id;
    }

    uint64_t key = ((uint64_t)src << 32) | (uint64_t)dst;

    if (edge_index_ != NULL) {
        auto iter = edge_index_->find(key);
        if (iter == edge_index_->end()) {
            return invalid_edge_id;
        }

        const std::vector<ui>* ids = iter->second;
        for (ui i = 0; i < ids->size(); ++i) {
            ui edge_id = (*ids)[i];
            if (edge_relations_[edge_id] == relation_id) {
                return edge_id;
            }
        }
        return invalid_edge_id;
    }

    const VertexID* begin_ptr = out_neighbors_ + out_offsets_[src];
    const VertexID* end_ptr = out_neighbors_ + out_offsets_[src + 1];
    const VertexID* it = std::lower_bound(begin_ptr, end_ptr, dst);
    if (it == end_ptr || *it != dst) {
        return invalid_edge_id;
    }

    while (it != end_ptr && *it == dst) {
        size_t pos = (size_t)(it - begin_ptr);
        ui edge_id = out_edge_ids_[out_offsets_[src] + pos];
        if (edge_relations_[edge_id] == relation_id) {
            return edge_id;
        }
        ++it;
    }

    return invalid_edge_id;
}

bool EmbeddingDiGraph::checkEdgeExistence(const VertexID src, const VertexID dst, const ui relation_id) const {
    return getEdgeId(src, dst, relation_id) != std::numeric_limits<ui>::max();
}

const float* EmbeddingDiGraph::getEdgeEmbeddingByVertices(const VertexID src, const VertexID dst, const ui relation_id) const {
    if (edge_embedding_dim_ == 0) {
        return NULL;
    }
    ui edge_id = getEdgeId(src, dst, relation_id);
    if (edge_id == std::numeric_limits<ui>::max()) {
        return NULL;
    }
    return getEdgeEmbeddingByEdgeId(edge_id);
}

std::unordered_map<LabelID, const float*> EmbeddingDiGraph::getVertexLabelEmbeddings(const VertexID v) const {
    std::unordered_map<LabelID, const float*> result;
    if (v >= vertices_count_ || vertex_label_offsets_ == NULL) {
        return result;
    }
    ui begin = vertex_label_offsets_[v];
    ui end = vertex_label_offsets_[v + 1];
    if (begin == end) {
        return result;
    }
    result.reserve(end - begin);
    for (ui i = begin; i < end; ++i) {
        LabelID label_id = vertex_labels_[i];
        const float* emb = getLabelEmbedding(label_id);
        result.emplace(label_id, emb);
    }
    return result;
}
