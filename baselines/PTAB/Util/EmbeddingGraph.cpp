#include "EmbeddingGraph.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

std::string JoinPath(const std::string &dir, const std::string &name) {
  return EmbeddingGraph::EnsureTrailingSlash(dir) + name;
}

bool Exists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

bool IsDirectoryPath(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string ParentPath(const std::string &path) {
  size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) return "";
  if (pos == 0) return "/";
  return path.substr(0, pos);
}

bool MakeDirectory(const std::string &path) {
  if (path.empty() || IsDirectoryPath(path)) return true;
  if (mkdir(path.c_str(), 0755) == 0) return true;
  return errno == EEXIST && IsDirectoryPath(path);
}

bool CreateDirectories(const std::string &path) {
  if (path.empty() || IsDirectoryPath(path)) return true;
  std::string current;
  size_t i = 0;
  if (path[0] == '/') {
    current = "/";
    i = 1;
  }
  while (i <= path.size()) {
    size_t next = path.find('/', i);
    std::string part = path.substr(i, next == std::string::npos ? next : next - i);
    if (!part.empty()) {
      if (!current.empty() && current[current.size() - 1] != '/') current += "/";
      current += part;
      if (!MakeDirectory(current)) return false;
    }
    if (next == std::string::npos) break;
    i = next + 1;
  }
  return true;
}

bool CopyFile(const std::string &from, const std::string &to) {
  if (!CreateDirectories(ParentPath(to))) return false;
  std::ifstream in(from.c_str(), std::ios::binary);
  std::ofstream out(to.c_str(), std::ios::binary | std::ios::trunc);
  if (!in.is_open() || !out.is_open()) return false;
  out << in.rdbuf();
  return static_cast<bool>(in) && static_cast<bool>(out);
}

bool CopyPath(const std::string &from, const std::string &to) {
  if (!Exists(from)) return true;
  if (!IsDirectoryPath(from)) return CopyFile(from, to);

  if (!CreateDirectories(to)) return false;
  DIR *dir = opendir(from.c_str());
  if (dir == nullptr) return false;
  bool ok = true;
  for (dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
    std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    if (!CopyPath(JoinPath(from, name), JoinPath(to, name))) {
      ok = false;
      break;
    }
  }
  closedir(dir);
  return ok;
}


bool SymlinkOrCopyPath(const std::string &from, const std::string &to) {
  if (!Exists(from)) return true;
  if (!CreateDirectories(ParentPath(to))) return false;
  if (symlink(from.c_str(), to.c_str()) == 0) return true;
  if (errno == EEXIST) return true;
  return CopyPath(from, to);
}

bool HasSuffix(const std::string &text, const std::string &suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsDigits(const std::string &text) {
  if (text.empty()) return false;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

std::vector<std::string> ListFilesWithSuffix(const std::string &dir_name,
                                        const std::string &suffix) {
  std::vector<std::string> files;
  DIR *dir = opendir(dir_name.c_str());
  if (dir == nullptr) return files;
  for (dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
    std::string name = entry->d_name;
    if (name == "." || name == ".." || !HasSuffix(name, suffix)) continue;
    files.push_back(JoinPath(dir_name, name));
  }
  closedir(dir);
  std::sort(files.begin(), files.end(), [&suffix](const std::string &a, const std::string &b) {
    std::string an = a.substr(a.find_last_of('/') + 1);
    std::string bn = b.substr(b.find_last_of('/') + 1);
    an = an.substr(0, an.size() - suffix.size());
    bn = bn.substr(0, bn.size() - suffix.size());
    if (IsDigits(an) && IsDigits(bn)) return std::stoul(an) < std::stoul(bn);
    return an < bn;
  });
  return files;
}

std::vector<std::string> ListBinFiles(const std::string &dir_name) {
  return ListFilesWithSuffix(dir_name, ".bin");
}

std::vector<std::string> ListTxtFiles(const std::string &dir_name) {
  return ListFilesWithSuffix(dir_name, ".txt");
}

bool ParseUint32(const std::string &text, uint32_t *value) {
  if (text.empty()) return false;
  char *end = nullptr;
  unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
  if (*end != '\0' || parsed > std::numeric_limits<uint32_t>::max()) return false;
  *value = static_cast<uint32_t>(parsed);
  return true;
}

void TrimRight(std::string *line) {
  while (!line->empty() && ((*line)[line->size() - 1] == '\r' ||
                            (*line)[line->size() - 1] == '\n')) {
    line->erase(line->size() - 1);
  }
}

std::vector<std::string> SplitTabFields(const std::string &line) {
  std::vector<std::string> fields;
  size_t begin = 0;
  while (true) {
    size_t pos = line.find('\t', begin);
    if (pos == std::string::npos) {
      fields.push_back(line.substr(begin));
      break;
    }
    fields.push_back(line.substr(begin, pos - begin));
    begin = pos + 1;
  }
  return fields;
}

std::string PercentEncode(const std::string &input) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setfill('0');
  for (unsigned char c : input) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') {
      out << c;
    } else {
      out << '%' << std::setw(2) << static_cast<int>(c);
    }
  }
  return out.str();
}

bool LoadNameToIdRaw(const std::string &file_name,
                     std::unordered_map<std::string, uint32_t> *name_to_id) {
  std::ifstream fin(file_name.c_str());
  if (!fin.is_open()) return false;
  name_to_id->clear();
  std::string line;
  uint32_t id = 0;
  while (std::getline(fin, line)) {
    TrimRight(&line);
    (*name_to_id)[line] = id++;
  }
  return id > 0;
}

bool LoadNameToIdPickFile(const std::string &file_name,
                          std::unordered_map<std::string, uint32_t> *name_to_id) {
  std::ifstream fin(file_name.c_str());
  if (!fin.is_open()) return false;
  std::string line;
  while (std::getline(fin, line)) {
    TrimRight(&line);
    if (line.empty()) continue;
    size_t pos = line.find_last_of(" \t");
    if (pos == std::string::npos) return false;
    std::string id_text = line.substr(pos + 1);
    std::string name = line.substr(0, pos);
    while (!name.empty() && (name[name.size() - 1] == ' ' || name[name.size() - 1] == '\t'))
      name.erase(name.size() - 1);
    uint32_t id = 0;
    if (!ParseUint32(id_text, &id)) return false;
    (*name_to_id)[name] = id;
  }
  return true;
}

bool LoadNameToIdPick(const std::string &file_name,
                      std::unordered_map<std::string, uint32_t> *name_to_id) {
  name_to_id->clear();
  if (IsDirectoryPath(file_name)) {
    auto files = ListTxtFiles(file_name);
    if (files.empty()) return false;
    for (const auto &path : files) {
      if (!LoadNameToIdPickFile(path, name_to_id)) return false;
    }
    return !name_to_id->empty();
  }
  if (!LoadNameToIdPickFile(file_name, name_to_id)) return false;
  return !name_to_id->empty();
}

bool LoadNameToIdByFormat(const std::string &file_name, EmbeddingGraph::Format format,
                          std::unordered_map<std::string, uint32_t> *name_to_id) {
  return format == EmbeddingGraph::Format::kPick
             ? LoadNameToIdPick(file_name, name_to_id)
             : LoadNameToIdRaw(file_name, name_to_id);
}

bool LoadIdToName(const std::string &file_name, EmbeddingGraph::Format format,
                  std::vector<std::string> *id_to_name) {
  std::unordered_map<std::string, uint32_t> name_to_id;
  if (!LoadNameToIdByFormat(file_name, format, &name_to_id))
    return false;

  uint32_t max_id = 0;
  for (const auto &pair : name_to_id) max_id = std::max(max_id, pair.second);
  id_to_name->assign(max_id + 1, "");
  for (const auto &pair : name_to_id) (*id_to_name)[pair.second] = pair.first;
  return true;
}

bool LoadNodeIdsById(const std::string &dir, EmbeddingGraph::Format format,
                     std::vector<std::string> *id_to_name) {
  return LoadIdToName(EmbeddingGraph::FindNodeNameToIdFile(dir, format), format, id_to_name);
}

bool LoadRelationIdsById(const std::string &dir, EmbeddingGraph::Format format,
                         std::vector<std::string> *id_to_name) {
  std::string file = format == EmbeddingGraph::Format::kPick
                         ? JoinPath(dir, "relation_name_to_id.txt")
                         : JoinPath(dir, "relation_names.txt");
  return LoadIdToName(file, format, id_to_name);
}

} 

namespace EmbeddingGraph {

const char *FormatName(Format format) {
  return format == Format::kPick ? "pick" : "raw";
}

const char *MetricName(Metric metric) {
  if (metric == Metric::kDotProduct) return "dot";
  if (metric == Metric::kSquaredL2) return "squared-l2";
  return "l2";
}

bool ParseMetric(const std::string &value, Metric *metric) {
  if (value == "l2" || value == "euclidean") {
    *metric = Metric::kL2;
    return true;
  }
  if (value == "squared-l2" || value == "sq-l2" ||
      value == "sqeuclidean" || value == "squared-euclidean") {
    *metric = Metric::kSquaredL2;
    return true;
  }
  if (value == "dot" || value == "inner" || value == "inner-product" ||
      value == "dot-product") {
    *metric = Metric::kDotProduct;
    return true;
  }
  return false;
}

bool VectorStore::Load(const std::string &file_name) {
  std::vector<std::string> files;
  if (IsDirectoryPath(file_name)) {
    files = ListBinFiles(file_name);
    if (files.empty()) return false;
  } else {
    files.push_back(file_name);
  }

  const char expected_magic[8] = {'E', 'M', 'B', 'E', 'D', 'B', 'I', 'N'};
  {
    std::ifstream probe(files[0].c_str(), std::ios::binary);
    if (!probe.is_open()) return false;
    char magic[8] = {0};
    probe.read(magic, sizeof(magic));
    if (!probe || std::memcmp(magic, expected_magic, sizeof(magic)) != 0) {
      if (files.size() != 1) return false;
      probe.close();
      std::ifstream fin(file_name.c_str(), std::ios::binary | std::ios::ate);
      if (!fin.is_open()) return false;
      std::streamoff file_size = fin.tellg();
      fin.seekg(0, std::ios::beg);
      fin.read(reinterpret_cast<char *>(&num), sizeof(uint32_t));
      fin.read(reinterpret_cast<char *>(&dim), sizeof(uint32_t));
      if (!fin || num == 0 || dim == 0) return false;
      uint64_t value_count = static_cast<uint64_t>(num) * dim;
      uint64_t expected_size = sizeof(uint32_t) * 2 + value_count * sizeof(float);
      if (expected_size != static_cast<uint64_t>(file_size)) return false;
      values.resize(static_cast<size_t>(num) * dim);
      fin.read(reinterpret_cast<char *>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(float)));
      return static_cast<bool>(fin);
    }
  }

  uint32_t loaded_dim = 0;
  uint64_t max_row_id = 0;
  bool saw_record = false;
  for (const std::string &path : files) {
    std::ifstream fin(path.c_str(), std::ios::binary);
    if (!fin.is_open()) return false;
    char magic[8] = {0};
    uint64_t count = 0;
    uint32_t dim_u32 = 0;
    fin.read(magic, sizeof(magic));
    fin.read(reinterpret_cast<char *>(&count), sizeof(uint64_t));
    fin.read(reinterpret_cast<char *>(&dim_u32), sizeof(uint32_t));
    if (!fin || std::memcmp(magic, expected_magic, sizeof(magic)) != 0 ||
        count == 0 || dim_u32 == 0) return false;
    if (loaded_dim == 0) loaded_dim = dim_u32;
    if (loaded_dim != dim_u32) return false;

    for (uint64_t i = 0; i < count; ++i) {
      uint32_t row_id = 0;
      uint32_t name_len = 0;
      fin.read(reinterpret_cast<char *>(&row_id), sizeof(uint32_t));
      fin.read(reinterpret_cast<char *>(&name_len), sizeof(uint32_t));
      if (!fin) return false;
      max_row_id = std::max<uint64_t>(max_row_id, row_id);
      saw_record = true;
      fin.seekg(static_cast<std::streamoff>(name_len) +
                    static_cast<std::streamoff>(loaded_dim) * sizeof(float),
                std::ios::cur);
      if (!fin) return false;
    }
  }
  if (!saw_record || loaded_dim == 0 ||
      max_row_id + 1 > std::numeric_limits<uint32_t>::max() ||
      max_row_id + 1 > std::numeric_limits<size_t>::max() / loaded_dim) {
    return false;
  }

  num = static_cast<uint32_t>(max_row_id + 1);
  dim = loaded_dim;
  values.assign(static_cast<size_t>(num) * dim, 0.0f);
  std::vector<float> scratch(dim, 0.0f);
  for (const std::string &path : files) {
    std::ifstream fin(path.c_str(), std::ios::binary);
    if (!fin.is_open()) return false;
    char magic[8] = {0};
    uint64_t count = 0;
    uint32_t dim_u32 = 0;
    fin.read(magic, sizeof(magic));
    fin.read(reinterpret_cast<char *>(&count), sizeof(uint64_t));
    fin.read(reinterpret_cast<char *>(&dim_u32), sizeof(uint32_t));
    if (!fin || std::memcmp(magic, expected_magic, sizeof(magic)) != 0 || dim_u32 != dim)
      return false;
    for (uint64_t i = 0; i < count; ++i) {
      uint32_t row_id = 0;
      uint32_t name_len = 0;
      fin.read(reinterpret_cast<char *>(&row_id), sizeof(uint32_t));
      fin.read(reinterpret_cast<char *>(&name_len), sizeof(uint32_t));
      if (!fin || row_id >= num) return false;
      fin.seekg(name_len, std::ios::cur);
      fin.read(reinterpret_cast<char *>(scratch.data()),
               static_cast<std::streamsize>(dim * sizeof(float)));
      if (!fin) return false;
      std::copy(scratch.begin(), scratch.end(),
                values.begin() + static_cast<size_t>(row_id) * dim);
    }
  }
  return true;
}

double VectorStore::Dot(uint32_t lhs, const VectorStore &rhs, uint32_t rhs_id) const {
  if (dim != rhs.dim || lhs >= num || rhs_id >= rhs.num) return 0.0;
  const float *a = values.data() + static_cast<size_t>(lhs) * dim;
  const float *b = rhs.values.data() + static_cast<size_t>(rhs_id) * rhs.dim;
  double result = 0.0;
  for (uint32_t i = 0; i < dim; ++i) result += static_cast<double>(a[i]) * b[i];
  return result;
}

double VectorStore::SquaredL2(uint32_t lhs, const VectorStore &rhs,
                              uint32_t rhs_id) const {
  if (dim != rhs.dim || lhs >= num || rhs_id >= rhs.num) return 0.0;
  const float *a = values.data() + static_cast<size_t>(lhs) * dim;
  const float *b = rhs.values.data() + static_cast<size_t>(rhs_id) * rhs.dim;
  double result = 0.0;
  for (uint32_t i = 0; i < dim; ++i) {
    double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    result += diff * diff;
  }
  return result;
}

double VectorStore::L2(uint32_t lhs, const VectorStore &rhs,
                       uint32_t rhs_id) const {
  return std::sqrt(SquaredL2(lhs, rhs, rhs_id));
}

std::string EnsureTrailingSlash(const std::string &dir) {
  if (dir.empty() || dir[dir.size() - 1] == '/') return dir;
  return dir + "/";
}

bool IsEmbeddingGraphDir(const std::string &dir, Format format) {
  if (!IsDirectoryPath(dir)) return false;
  if (format == Format::kPick) {
    return Exists(JoinPath(dir, "FactKG.graph.txt")) &&
           Exists(JoinPath(dir, "relation_name_to_id.txt")) &&
           !FindNodeNameToIdFile(dir, format).empty() &&
           !FindNodeEmbeddingFile(dir, format).empty();
  }
  return Exists(JoinPath(dir, "edges.txt")) &&
         Exists(JoinPath(dir, "node_names.txt")) &&
         Exists(JoinPath(dir, "relation_names.txt")) &&
         !FindNodeEmbeddingFile(dir, format).empty();
}

std::string NodeTerm(const std::string &name) {
  return "<pteb-node:" + PercentEncode(name) + ">";
}

std::string TypeTerm(const std::string &name) {
  return "<pteb-type:" + PercentEncode(name) + ">";
}

std::string RelationTerm(const std::string &name) {
  return "<pteb-rel:" + PercentEncode(name) + ">";
}

bool ConvertGraphDirToRdf(const std::string &graph_dir, const std::string &rdf_file,
                          Format format) {
  std::ofstream out(rdf_file.c_str());
  if (!out.is_open()) return false;

  if (format == Format::kPick) {
    std::ifstream type_file(JoinPath(graph_dir, "FactKG.type_to_nodes.txt").c_str());
    std::string line;
    while (std::getline(type_file, line)) {
      TrimRight(&line);
      if (line.empty()) continue;
      auto fields = SplitTabFields(line);
      if (fields.size() != 2) continue;
      out << NodeTerm(fields[1])
          << " <http://www.w3.org/1999/02/22-rdf-syntax-ns#type> "
          << TypeTerm(fields[0]) << " .\n";
    }

    std::ifstream edge_file(JoinPath(graph_dir, "FactKG.graph.txt").c_str());
    while (std::getline(edge_file, line)) {
      TrimRight(&line);
      if (line.empty()) continue;
      auto fields = SplitTabFields(line);
      if (fields.size() != 3) continue;
      out << NodeTerm(fields[0]) << " " << RelationTerm(fields[1]) << " "
          << NodeTerm(fields[2]) << " .\n";
    }
  } else {
    std::vector<std::string> nodes;
    std::vector<std::string> relations;
    if (!LoadNodeIdsById(graph_dir, format, &nodes) ||
        !LoadRelationIdsById(graph_dir, format, &relations)) {
      return false;
    }

    std::ifstream edge_file(JoinPath(graph_dir, "edges.txt").c_str());
    uint32_t src = 0, rel = 0, dst = 0;
    while (edge_file >> src >> rel >> dst) {
      if (src >= nodes.size() || dst >= nodes.size() || rel >= relations.size()) return false;
      out << NodeTerm(nodes[src]) << " " << RelationTerm(relations[rel]) << " "
          << NodeTerm(nodes[dst]) << " .\n";
      if (src != dst) {
        out << NodeTerm(nodes[dst]) << " " << RelationTerm(relations[rel]) << " "
            << NodeTerm(nodes[src]) << " .\n";
      }
    }
  }

  return static_cast<bool>(out);
}

bool CopyMetadataToStore(const std::string &graph_dir, const std::string &db_store_dir,
                         Format format) {
  std::string target = JoinPath(db_store_dir, "EmbeddingGraph");
  if (!CreateDirectories(target)) return false;

  if (format == Format::kRaw) {
    const char *files[] = {
        "edges.txt",
        "node_names.txt",
        "relation_names.txt",
        "stats.txt",
        "node_embeddings.bin",
        "relation_embeddings.bin"};
    for (const char *file : files) {
      if (!CopyPath(JoinPath(graph_dir, file), JoinPath(target, file))) return false;
    }
    return true;
  }

  const char *files[] = {
      "FactKG.graph.txt",
      "FactKG.type_to_nodes.txt",
      "FactKG.queries_gt.txt",
      "relation_name_to_id.txt",
      "type_name_to_id.txt",
      "relation_embeddings.bin",
      "type_embeddings.bin"};
  for (const char *file : files) {
    if (!CopyPath(JoinPath(graph_dir, file), JoinPath(target, file))) return false;
  }
  if (!Exists(JoinPath(target, "FactKG.queries_gt.txt")) &&
      Exists(JoinPath(graph_dir, "LCQuAD.queries_gt.txt")) &&
      !CopyPath(JoinPath(graph_dir, "LCQuAD.queries_gt.txt"),
                JoinPath(target, "FactKG.queries_gt.txt"))) {
    return false;
  }
  if (!SymlinkOrCopyPath(JoinPath(graph_dir, "node_name_to_id"),
                         JoinPath(target, "node_name_to_id")))
    return false;
  if (!SymlinkOrCopyPath(JoinPath(graph_dir, "node_embeddings"),
                         JoinPath(target, "node_embeddings")))
    return false;
  if (Exists(JoinPath(graph_dir, "node_embeddings.bin")) &&
      !CopyPath(JoinPath(graph_dir, "node_embeddings.bin"),
                JoinPath(target, "node_embeddings.bin")))
    return false;
  return true;
}

bool GenerateQuerySparql(const std::string &query_dir, int k, std::string *sparql,
                         Format format) {
  std::vector<std::string> id_to_name;
  if (!LoadNodeIdsById(query_dir, format, &id_to_name)) return false;

  std::ostringstream out;
  out << "select";
  for (size_t i = 0; i < id_to_name.size(); ++i) out << " ?v" << i;
  out << " where {\n";

  std::unordered_map<std::string, uint32_t> node_to_id;
  if (!LoadNameToIdByFormat(FindNodeNameToIdFile(query_dir, format), format, &node_to_id))
    return false;

  if (format == Format::kPick) {
    std::ifstream type_file(JoinPath(query_dir, "FactKG.type_to_nodes.txt").c_str());
    std::string line;
    while (std::getline(type_file, line)) {
      TrimRight(&line);
      if (line.empty()) continue;
      auto fields = SplitTabFields(line);
      if (fields.size() != 2) continue;
      auto it = node_to_id.find(fields[1]);
      if (it == node_to_id.end()) continue;
      out << "  ?v" << it->second
          << " <http://www.w3.org/1999/02/22-rdf-syntax-ns#type> "
          << TypeTerm(fields[0]) << " .\n";
    }

    std::ifstream edge_file(JoinPath(query_dir, "FactKG.graph.txt").c_str());
    while (std::getline(edge_file, line)) {
      TrimRight(&line);
      if (line.empty()) continue;
      auto fields = SplitTabFields(line);
      if (fields.size() != 3) continue;
      auto src_it = node_to_id.find(fields[0]);
      auto dst_it = node_to_id.find(fields[2]);
      if (src_it == node_to_id.end() || dst_it == node_to_id.end()) continue;
      out << "  ?v" << src_it->second << " " << RelationTerm(fields[1])
          << " ?v" << dst_it->second << " .\n";
    }
  } else {
    std::vector<std::string> relations;
    if (!LoadRelationIdsById(query_dir, format, &relations)) return false;
    std::ifstream edge_file(JoinPath(query_dir, "edges.txt").c_str());
    uint32_t src = 0, rel = 0, dst = 0;
    while (edge_file >> src >> rel >> dst) {
      if (src >= id_to_name.size() || dst >= id_to_name.size() ||
          rel >= relations.size()) {
        return false;
      }
      out << "  ?v" << src << " " << RelationTerm(relations[rel])
          << " ?v" << dst << " .\n";
    }
  }
  out << "} order by desc(";
  for (size_t i = 0; i < id_to_name.size(); ++i) {
    if (i != 0) out << " + ";
    out << "?v" << i;
  }
  out << ") limit " << k << "\n";
  *sparql = out.str();
  return true;
}

bool LoadNameToId(const std::string &file_name,
                  std::unordered_map<std::string, uint32_t> *name_to_id) {
  if (file_name.empty()) return false;
  if (LoadNameToIdPick(file_name, name_to_id)) return true;
  return LoadNameToIdRaw(file_name, name_to_id);
}

std::string FindNodeNameToIdFile(const std::string &dir, Format format) {
  if (format == Format::kRaw) {
    std::string raw = JoinPath(dir, "node_names.txt");
    if (Exists(raw)) return raw;
    return "";
  }
  std::string direct = JoinPath(dir, "node_name_to_id.txt");
  if (Exists(direct)) return direct;
  std::string nested_dir = JoinPath(dir, "node_name_to_id");
  if (IsDirectoryPath(nested_dir)) return nested_dir;
  std::string nested = JoinPath(nested_dir, "0.txt");
  if (Exists(nested)) return nested;
  return "";
}

std::string FindNodeEmbeddingFile(const std::string &dir, Format format) {
  if (format == Format::kRaw) {
    std::string raw = JoinPath(dir, "node_embeddings.bin");
    if (Exists(raw)) return raw;
    return "";
  }
  std::string direct = JoinPath(dir, "node_embeddings.bin");
  if (Exists(direct)) return direct;
  std::string nested_dir = JoinPath(dir, "node_embeddings");
  if (IsDirectoryPath(nested_dir)) return nested_dir;
  std::string nested = JoinPath(nested_dir, "0.bin");
  if (Exists(nested)) return nested;
  return "";
}

bool ScoreContext::LoadData(const std::string &data_dir, Format format,
                            Metric metric) {
  data_loaded_ = false;
  enabled_ = false;
  metric_ = metric;
  last_error_.clear();
  data_term_to_row_.clear();
  data_relation_term_to_row_.clear();
  query_var_to_row_.clear();
  score_cache_.clear();
  precomputed_scores_.clear();
  precomputed_scores_enabled_ = false;
  query_vectors_ = VectorStore();

  if (!data_vectors_.Load(FindNodeEmbeddingFile(data_dir, format))) {
    last_error_ = "failed to load data node embeddings";
    return false;
  }

  std::unordered_map<std::string, uint32_t> data_name_to_id;
  if (!LoadNameToIdByFormat(FindNodeNameToIdFile(data_dir, format), format,
                            &data_name_to_id)) {
    last_error_ = "failed to load data node_name_to_id";
    return false;
  }

  for (const auto &pair : data_name_to_id) {
    data_term_to_row_[NodeTerm(pair.first)] = pair.second;
  }
  std::vector<std::string> data_relations;
  if (LoadRelationIdsById(data_dir, format, &data_relations)) {
    for (uint32_t id = 0; id < data_relations.size(); ++id) {
      if (!data_relations[id].empty())
        data_relation_term_to_row_[RelationTerm(data_relations[id])] = id;
    }
  }
  data_loaded_ = true;
  return true;
}

bool ScoreContext::LoadQuery(const std::string &query_dir, Format format) {
  enabled_ = false;
  last_error_.clear();
  query_var_to_row_.clear();
  score_cache_.clear();
  precomputed_scores_.clear();
  precomputed_scores_enabled_ = false;
  query_vectors_ = VectorStore();

  if (!data_loaded_) {
    last_error_ = "data embedding context is not loaded";
    return false;
  }
  if (!query_vectors_.Load(FindNodeEmbeddingFile(query_dir, format))) {
    last_error_ = "failed to load query node embeddings";
    return false;
  }
  if (data_vectors_.dim != query_vectors_.dim) {
    last_error_ = "data/query embedding dimensions do not match";
    return false;
  }

  std::unordered_map<std::string, uint32_t> query_name_to_id;
  if (!LoadNameToIdByFormat(FindNodeNameToIdFile(query_dir, format), format,
                            &query_name_to_id)) {
    last_error_ = "failed to load query node_name_to_id";
    return false;
  }
  for (const auto &pair : query_name_to_id) {
    query_var_to_row_["?v" + std::to_string(pair.second)] = pair.second;
  }
  enabled_ = true;
  return true;
}

bool ScoreContext::Load(const std::string &data_dir, const std::string &query_dir,
                        Format format,
                        Metric metric) {
  return LoadData(data_dir, format, metric) && LoadQuery(query_dir, format);
}

uint64_t ScoreContext::PrecomputedScoreCount() const {
  return static_cast<uint64_t>(precomputed_scores_.size());
}

bool ScoreContext::PrecomputeAllScores() {
  if (!enabled_) {
    last_error_ = "score context is not enabled";
    return false;
  }
  if (query_vectors_.num == 0 || data_vectors_.num == 0 ||
      query_vectors_.dim != data_vectors_.dim) {
    last_error_ = "invalid data/query vectors for score precompute";
    return false;
  }
  uint64_t score_count = static_cast<uint64_t>(query_vectors_.num) * data_vectors_.num;
  if (query_vectors_.num != 0 && score_count / query_vectors_.num != data_vectors_.num) {
    last_error_ = "precomputed score matrix is too large";
    return false;
  }
  if (score_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max() / sizeof(double))) {
    last_error_ = "precomputed score matrix exceeds addressable memory";
    return false;
  }

  precomputed_scores_.assign(static_cast<size_t>(score_count), 0.0);
  for (uint32_t query_row = 0; query_row < query_vectors_.num; ++query_row) {
    double *row = precomputed_scores_.data() +
                  static_cast<size_t>(query_row) * data_vectors_.num;
    for (uint32_t data_row = 0; data_row < data_vectors_.num; ++data_row) {
      if (metric_ == Metric::kDotProduct) {
        row[data_row] = query_vectors_.Dot(query_row, data_vectors_, data_row);
      } else if (metric_ == Metric::kSquaredL2) {
        row[data_row] = -query_vectors_.SquaredL2(query_row, data_vectors_, data_row);
      } else {
        row[data_row] = -query_vectors_.L2(query_row, data_vectors_, data_row);
      }
    }
  }
  score_cache_.clear();
  precomputed_scores_enabled_ = true;
  return true;
}

bool ScoreContext::GetScore(const std::string &query_var,
                            unsigned data_node_id,
                            const std::string &data_node_term,
                            double *score) {
  if (!enabled_) return false;
  auto var_it = query_var_to_row_.find(query_var);
  if (var_it == query_var_to_row_.end()) return false;

  auto &var_cache = score_cache_[query_var];
  auto cache_it = var_cache.find(data_node_id);
  if (cache_it != var_cache.end()) {
    *score = cache_it->second;
    return true;
  }

  auto data_it = data_term_to_row_.find(data_node_term);
  if (data_it == data_term_to_row_.end()) return false;
  double value = 0.0;
  if (precomputed_scores_enabled_) {
    uint32_t query_row = var_it->second;
    uint32_t data_row = data_it->second;
    if (query_row >= query_vectors_.num || data_row >= data_vectors_.num) return false;
    value = precomputed_scores_[static_cast<size_t>(query_row) * data_vectors_.num + data_row];
  } else if (metric_ == Metric::kDotProduct) {
    value = query_vectors_.Dot(var_it->second, data_vectors_, data_it->second);
  } else if (metric_ == Metric::kSquaredL2) {
    
    
    value = -query_vectors_.SquaredL2(var_it->second, data_vectors_, data_it->second);
  } else {
    
    
    value = -query_vectors_.L2(var_it->second, data_vectors_, data_it->second);
  }
  var_cache[data_node_id] = value;
  *score = value;
  return true;
}

bool ScoreContext::GetRawNodeId(const std::string &data_node_term,
                                uint32_t *raw_id) const {
  auto it = data_term_to_row_.find(data_node_term);
  if (it == data_term_to_row_.end()) return false;
  *raw_id = it->second;
  return true;
}

bool ScoreContext::GetRawRelationId(const std::string &data_relation_term,
                                    uint32_t *raw_id) const {
  auto it = data_relation_term_to_row_.find(data_relation_term);
  if (it == data_relation_term_to_row_.end()) return false;
  *raw_id = it->second;
  return true;
}

} 
