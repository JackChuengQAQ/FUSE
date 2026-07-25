#ifndef PTEB_UTIL_EMBEDDING_GRAPH_H_
#define PTEB_UTIL_EMBEDDING_GRAPH_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace EmbeddingGraph {

enum class Format {
  kRaw,
  kPick,
};

enum class Metric {
  kL2,
  kSquaredL2,
  kDotProduct,
};

struct VectorStore {
  uint32_t num = 0;
  uint32_t dim = 0;
  std::vector<float> values;

  bool Load(const std::string &file_name);
  double Dot(uint32_t lhs, const VectorStore &rhs, uint32_t rhs_id) const;
  double SquaredL2(uint32_t lhs, const VectorStore &rhs, uint32_t rhs_id) const;
  double L2(uint32_t lhs, const VectorStore &rhs, uint32_t rhs_id) const;
};

bool IsEmbeddingGraphDir(const std::string &dir, Format format = Format::kRaw);
const char *FormatName(Format format);
const char *MetricName(Metric metric);
bool ParseMetric(const std::string &value, Metric *metric);
std::string EnsureTrailingSlash(const std::string &dir);
std::string NodeTerm(const std::string &name);
std::string TypeTerm(const std::string &name);
std::string RelationTerm(const std::string &name);

bool ConvertGraphDirToRdf(const std::string &graph_dir, const std::string &rdf_file,
                          Format format = Format::kRaw);
bool CopyMetadataToStore(const std::string &graph_dir, const std::string &db_store_dir,
                         Format format = Format::kRaw);
bool GenerateQuerySparql(const std::string &query_dir, int k, std::string *sparql,
                         Format format = Format::kRaw);

bool LoadNameToId(const std::string &file_name,
                  std::unordered_map<std::string, uint32_t> *name_to_id);
std::string FindNodeNameToIdFile(const std::string &dir, Format format = Format::kRaw);
std::string FindNodeEmbeddingFile(const std::string &dir, Format format = Format::kRaw);

class ScoreContext {
 public:
  bool LoadData(const std::string &data_dir,
                Format format = Format::kRaw,
                Metric metric = Metric::kL2);
  bool LoadQuery(const std::string &query_dir,
                 Format format = Format::kRaw);
  bool Load(const std::string &data_dir, const std::string &query_dir,
            Format format = Format::kRaw,
            Metric metric = Metric::kL2);
  bool PrecomputeAllScores();
  bool Enabled() const { return enabled_; }
  bool HasPrecomputedScores() const { return precomputed_scores_enabled_; }
  uint64_t PrecomputedScoreCount() const;
  bool GetScore(const std::string &query_var, unsigned data_node_id,
                const std::string &data_node_term, double *score);
  bool GetRawNodeId(const std::string &data_node_term, uint32_t *raw_id) const;
  bool GetRawRelationId(const std::string &data_relation_term, uint32_t *raw_id) const;
  const std::string &LastError() const { return last_error_; }
  Metric metric() const { return metric_; }

 private:
  bool data_loaded_ = false;
  bool enabled_ = false;
  bool precomputed_scores_enabled_ = false;
  Metric metric_ = Metric::kL2;
  std::string last_error_;
  VectorStore data_vectors_;
  VectorStore query_vectors_;
  std::vector<double> precomputed_scores_;
  std::unordered_map<std::string, uint32_t> data_term_to_row_;
  std::unordered_map<std::string, uint32_t> data_relation_term_to_row_;
  std::unordered_map<std::string, uint32_t> query_var_to_row_;
  std::unordered_map<std::string,
                     std::unordered_map<unsigned, double>> score_cache_;
};

} 

#endif 
