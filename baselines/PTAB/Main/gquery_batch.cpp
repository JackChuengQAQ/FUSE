#include "../Database/Database.h"
#include "../Query/topk/TopKUtil.h"
#include "../Util/EmbeddingGraph.h"
#include "../Util/Util.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

struct Options {
  std::string db_name;
  std::string query_base;
  std::string out_base;
  std::string run_id;
  std::string method_name = "PTAB";
  std::vector<int> sizes = {4, 8, 12, 16, 20};
  int topk = 3;
  int start_id = 1;
  int end_id = 100;
  int id_width = 3;
  int time_limit_sec = 3600;
  int mem_limit_pct = 93;
  int mem_limit_gib = 0;
  int mem_poll_sec = 5;
  bool pick = true;
  bool global_scores = false;
  EmbeddingGraph::Metric metric = EmbeddingGraph::Metric::kL2;
};

struct Counts {
  int done = 0;
  int skipped = 0;
  int timeout = 0;
  int failed = 0;
  int memstop = 0;
  int oom = 0;
};

enum class StopReason {
  kNone,
  kTimeout,
  kMemory,
  kOom,
  kShutdown,
};

struct WaitResult {
  int status = 0;
  StopReason reason = StopReason::kNone;
  int used_pct = 0;
  long long rss_kib = 0;
};

volatile sig_atomic_t g_stop_requested = 0;
volatile sig_atomic_t g_active_child = -1;

void SignalHandler(int) {
  g_stop_requested = 1;
  if (g_active_child > 0) {
    kill(-static_cast<pid_t>(g_active_child), SIGTERM);
  }
}

std::string JoinPath(const std::string &dir, const std::string &name) {
  if (dir.empty()) return name;
  return dir[dir.size() - 1] == '/' ? dir + name : dir + "/" + name;
}

bool IsDirectory(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool FileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool FileNonEmpty(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

bool MakeDirectory(const std::string &path) {
  if (path.empty() || IsDirectory(path)) return true;
  if (mkdir(path.c_str(), 0755) == 0) return true;
  return errno == EEXIST && IsDirectory(path);
}

bool CreateDirectories(const std::string &path) {
  if (path.empty() || IsDirectory(path)) return true;
  std::string current;
  size_t pos = 0;
  if (path[0] == '/') {
    current = "/";
    pos = 1;
  }
  while (pos <= path.size()) {
    size_t next = path.find('/', pos);
    std::string part = path.substr(pos, next == std::string::npos ? next : next - pos);
    if (!part.empty()) {
      if (!current.empty() && current[current.size() - 1] != '/') current += "/";
      current += part;
      if (!MakeDirectory(current)) return false;
    }
    if (next == std::string::npos) break;
    pos = next + 1;
  }
  return true;
}

std::string FormatId(int id, int width) {
  std::ostringstream out;
  out << std::setw(width) << std::setfill('0') << id;
  return out.str();
}

std::string Timestamp(bool compact = false) {
  time_t now = time(nullptr);
  struct tm value;
  localtime_r(&now, &value);
  char buffer[64];
  strftime(buffer, sizeof(buffer), compact ? "%Y%m%d_%H%M%S" : "%Y-%m-%dT%H:%M:%S%z", &value);
  return buffer;
}

std::vector<int> ParseIntList(const std::string &value) {
  std::vector<int> result;
  std::stringstream input(value);
  std::string item;
  while (std::getline(input, item, ',')) {
    if (!item.empty()) result.push_back(std::atoi(item.c_str()));
  }
  return result;
}

bool ParsePositiveInt(const char *value, int *out) {
  char *end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  if (end == value || *end != '\0' || parsed <= 0 || parsed > 2147483647L) return false;
  *out = static_cast<int>(parsed);
  return true;
}

void PrintUsage(const char *program) {
  std::cerr
      << "Usage: " << program << " DB_NAME QUERY_BASE OUT_BASE [options]\n"
      << "Options:\n"
      << "  --sizes 4,8,12,16,20\n"
      << "  --method PTAB\n"
      << "  --topk 3\n"
      << "  --start-id 1 --end-id 100 --id-width 3\n"
      << "  --time-limit 3600\n"
      << "  --mem-limit-pct 93 | --mem-limit-gib 100 --mem-poll-sec 5\n"
      << "  --metric l2|squared-l2|dot\n"
      << "  --pick | --raw\n"
      << "  --global-scores\n"
      << "  --run-id ID\n";
}

bool ParseArgs(int argc, char **argv, Options *options) {
  if (argc < 4) return false;
  options->db_name = argv[1];
  options->query_base = argv[2];
  options->out_base = argv[3];
  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--sizes" && i + 1 < argc) {
      options->sizes = ParseIntList(argv[++i]);
      if (options->sizes.empty()) return false;
    } else if (arg == "--method" && i + 1 < argc) {
      options->method_name = argv[++i];
    } else if (arg == "--topk" && i + 1 < argc) {
      if (!ParsePositiveInt(argv[++i], &options->topk)) return false;
    } else if (arg == "--start-id" && i + 1 < argc) {
      if (!ParsePositiveInt(argv[++i], &options->start_id)) return false;
    } else if (arg == "--end-id" && i + 1 < argc) {
      if (!ParsePositiveInt(argv[++i], &options->end_id)) return false;
    } else if (arg == "--id-width" && i + 1 < argc) {
      if (!ParsePositiveInt(argv[++i], &options->id_width)) return false;
    } else if (arg == "--time-limit" && i + 1 < argc) {
      if (!ParsePositiveInt(argv[++i], &options->time_limit_sec)) return false;
    } else if (arg == "--mem-limit-pct" && i + 1 < argc) {
      if (!ParsePositiveInt(argv[++i], &options->mem_limit_pct) ||
          options->mem_limit_pct > 100) return false;
    } else if (arg == "--mem-limit-gib" && i + 1 < argc) {
      if (!ParsePositiveInt(argv[++i], &options->mem_limit_gib)) return false;
    } else if (arg == "--mem-poll-sec" && i + 1 < argc) {
      if (!ParsePositiveInt(argv[++i], &options->mem_poll_sec)) return false;
    } else if (arg == "--metric" && i + 1 < argc) {
      if (!EmbeddingGraph::ParseMetric(argv[++i], &options->metric)) return false;
    } else if (arg == "--pick") {
      options->pick = true;
    } else if (arg == "--raw") {
      options->pick = false;
    } else if (arg == "--global-scores") {
      options->global_scores = true;
    } else if (arg == "--run-id" && i + 1 < argc) {
      options->run_id = argv[++i];
    } else {
      return false;
    }
  }
  if (options->end_id < options->start_id) return false;
  if (options->run_id.empty()) options->run_id = "pteb_persistent_" + Timestamp(true);
  return true;
}

bool ParseMethod(const std::string &name, TopKStrategy *method) {
  if (name == "DP-B") *method = TopKStrategy::DP_B;
  else if (name == "DP-P") *method = TopKStrategy::DP_P;
  else if (name == "kTPM") *method = TopKStrategy::K_TPM;
  else if (name == "DFS-OLD") *method = TopKStrategy::DFS_OLD;
  else if (name == "PTAB") *method = TopKStrategy::PTAB;
  else if (name == "Take-all") *method = TopKStrategy::TAKE_ALL;
  else if (name == "Take2") *method = TopKStrategy::TAKE2;
  else if (name == "Eager") *method = TopKStrategy::EAGER;
  else if (name == "SAE") *method = TopKStrategy::SAE;
  else if (name == "Naive") *method = TopKStrategy::RankAfterMatching;
  else return false;
  return true;
}

int MemoryUsedPct() {
  std::ifstream input("/proc/meminfo");
  std::string key;
  long long value = 0;
  std::string unit;
  long long total = 0;
  long long available = 0;
  while (input >> key >> value >> unit) {
    if (key == "MemTotal:") total = value;
    else if (key == "MemAvailable:") available = value;
    if (total > 0 && available > 0) break;
  }
  if (total <= 0) return 0;
  return static_cast<int>(((total - available) * 100 + total / 2) / total);
}

long long ProcessRssKiB(pid_t pid) {
  std::ifstream input(("/proc/" + std::to_string(pid) + "/status").c_str());
  std::string line;
  while (std::getline(input, line)) {
    if (line.compare(0, 6, "VmRSS:") != 0) continue;
    std::istringstream values(line.substr(6));
    long long value = 0;
    std::string unit;
    if (values >> value >> unit) return value;
  }
  return 0;
}

bool WaitForMemory(int limit_pct) {
  while (!g_stop_requested) {
    int used = MemoryUsedPct();
    if (used < limit_pct) return true;
    std::cout << "[mem-wait] used=" << used << "% >= " << limit_pct << "%" << std::endl;
    for (int i = 0; i < 30 && !g_stop_requested; ++i) sleep(1);
  }
  return false;
}

void AppendLine(const std::string &path, const std::string &line) {
  std::ofstream output(path.c_str(), std::ios::app);
  output << line << '\n';
}

void AppendLog(const std::string &path, const std::string &line) {
  std::ofstream output(path.c_str(), std::ios::app);
  output << line << '\n';
}

int RunOneQuery(Database *database,
                const std::shared_ptr<EmbeddingGraph::ScoreContext> &score_context,
                const Options &options,
                TopKStrategy method,
                const std::string &query_path,
                const std::string &tmp_output) {
  EmbeddingGraph::Format format = options.pick ? EmbeddingGraph::Format::kPick
                                               : EmbeddingGraph::Format::kRaw;
  std::string query;
  if (!EmbeddingGraph::GenerateQuerySparql(query_path, options.topk, &query, format)) {
    std::cerr << "generate embedding query SPARQL failed: " << query_path << std::endl;
    return 2;
  }
  if (!score_context->LoadQuery(query_path, format)) {
    std::cerr << "query embedding context failed: " << score_context->LastError() << std::endl;
    return 2;
  }

  long score_precompute_ms = 0;
  if (options.global_scores) {
    long begin = Util::get_cur_time();
    if (!score_context->PrecomputeAllScores()) {
      std::cerr << "global embedding score precompute failed: "
                << score_context->LastError() << std::endl;
      return 2;
    }
    score_precompute_ms = Util::get_cur_time() - begin;
    std::cout << "global embedding score matrix precomputed "
              << score_context->PrecomputedScoreCount() << " scores, used "
              << score_precompute_ms << "ms." << std::endl;
  }

  TopKUtil::SetEmbeddingScoreContext(score_context);
  TopKUtil::ClearGroundTruthOutputLines();
  std::cout << EmbeddingGraph::FormatName(format)
            << " embedding score context loaded with metric "
            << EmbeddingGraph::MetricName(options.metric)
            << (score_context->HasPrecomputedScores() ? " using global precomputed scores"
                                                      : " using lazy scores")
            << " from preloaded KG data and " << query_path << std::endl;
  std::cout << "query is:\n" << query << std::endl;
  std::cout << "Top-k method: " << options.method_name << std::endl;

  FILE *output = fopen(tmp_output.c_str(), "w");
  if (output == nullptr) {
    std::cerr << "cannot open temporary output: " << tmp_output << std::endl;
    return 2;
  }

  ResultSet result;
  database->setEmbeddingPrecomputeTime(score_precompute_ms);
  int ret = database->query(query, result, output, true, false, nullptr, method);
  fclose(output);

  if (TopKUtil::HasGroundTruthOutputLines()) {
    FILE *ground_truth = fopen(tmp_output.c_str(), "w");
    if (ground_truth == nullptr || !TopKUtil::WriteGroundTruthOutput(ground_truth)) {
      if (ground_truth != nullptr) fclose(ground_truth);
      std::cerr << "failed to write ground-truth style output: " << tmp_output << std::endl;
      return 2;
    }
    fclose(ground_truth);
  }

  if (ret != -100) {
    std::cerr << "query failed, Database::query returned " << ret << std::endl;
    return 3;
  }
  return 0;
}

pid_t StartChild(Database *database,
                 const std::shared_ptr<EmbeddingGraph::ScoreContext> &score_context,
                 const Options &options,
                 TopKStrategy method,
                 const std::string &query_path,
                 const std::string &tmp_output,
                 const std::string &log_path) {
  fflush(nullptr);
  pid_t pid = fork();
  if (pid != 0) {
    if (pid > 0) setpgid(pid, pid);
    return pid;
  }

  if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0) _exit(126);
  if (getppid() == 1) _exit(143);
  setpgid(0, 0);
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);
  int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) _exit(125);
  dup2(fd, STDOUT_FILENO);
  dup2(fd, STDERR_FILENO);
  close(fd);

  std::cout << "gquery_batch child pid=" << getpid() << std::endl;
  int rc = RunOneQuery(database, score_context, options, method, query_path, tmp_output);
  std::cout.flush();
  std::cerr.flush();
  _exit(rc);
}

WaitResult WaitForChild(pid_t pid, const Options &options) {
  WaitResult result;
  const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point next_mem_check = begin;
  bool terminate_sent = false;
  std::chrono::steady_clock::time_point terminate_at;

  while (true) {
    pid_t done = waitpid(pid, &result.status, WNOHANG);
    if (done == pid) return result;
    if (done < 0 && errno != EINTR) {
      result.status = 125 << 8;
      return result;
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const long elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - begin).count();
    if (!terminate_sent) {
      if (g_stop_requested) {
        result.reason = StopReason::kShutdown;
      } else if (elapsed >= options.time_limit_sec) {
        result.reason = StopReason::kTimeout;
      } else if (now >= next_mem_check) {
        if (options.mem_limit_gib > 0) {
          result.rss_kib = ProcessRssKiB(pid);
          const long long limit_kib =
              static_cast<long long>(options.mem_limit_gib) * 1024LL * 1024LL;
          if (result.rss_kib >= limit_kib) {
            result.reason = StopReason::kOom;
          }
        } else {
          result.used_pct = MemoryUsedPct();
          if (result.used_pct >= options.mem_limit_pct) {
            result.reason = StopReason::kMemory;
          }
        }
        next_mem_check = now + std::chrono::seconds(options.mem_poll_sec);
      }
      if (result.reason != StopReason::kNone) {
        kill(-pid, SIGTERM);
        terminate_sent = true;
        terminate_at = now;
      }
    } else if (std::chrono::duration_cast<std::chrono::seconds>(now - terminate_at).count() >= 10) {
      kill(-pid, SIGKILL);
    }
    usleep(200000);
  }
}

int ExitCode(const WaitResult &result) {
  if (WIFEXITED(result.status)) return WEXITSTATUS(result.status);
  if (WIFSIGNALED(result.status)) return 128 + WTERMSIG(result.status);
  return 125;
}

void WriteSummary(const std::string &path,
                  const Options &options,
                  int size,
                  const Counts &counts,
                  const std::string &progress) {
  std::ofstream output(path.c_str(), std::ios::trunc);
  output << "finished " << Timestamp() << '\n'
         << "db=" << options.db_name << '\n'
         << "queries=" << JoinPath(options.query_base, "n" + std::to_string(size)) << '\n'
         << "out=" << JoinPath(options.out_base, "n" + std::to_string(size)) << '\n'
         << "method=" << options.method_name << " topk=" << options.topk
         << " metric=" << EmbeddingGraph::MetricName(options.metric)
         << " global_scores=" << (options.global_scores ? 1 : 0) << '\n'
         << "persistent_preload=1 fork_per_query=1\n"
         << "time_limit_sec=" << options.time_limit_sec
         << " workers=1";
  if (options.mem_limit_gib > 0) {
    output << " mem_limit_gib=" << options.mem_limit_gib
           << " memory_metric=VmRSS";
  } else {
    output << " mem_limit_pct=" << options.mem_limit_pct;
  }
  output << " mem_poll_sec=" << options.mem_poll_sec << '\n'
         << "done=" << counts.done << " skip=" << counts.skipped
         << " timeout=" << counts.timeout << " fail=" << counts.failed
         << " memstop=" << counts.memstop << " oom=" << counts.oom << '\n'
         << "progress=" << progress << '\n';
}

}  

int main(int argc, char **argv) {
  Util util;
  Options options;
  if (!ParseArgs(argc, argv, &options)) {
    PrintUsage(argv[0]);
    return 2;
  }
  TopKStrategy method;
  if (!ParseMethod(options.method_name, &method)) {
    std::cerr << "unsupported method: " << options.method_name << std::endl;
    return 2;
  }

  signal(SIGTERM, SignalHandler);
  signal(SIGINT, SignalHandler);

  std::cout << "=== persistent PTEB batch ===\n"
            << "db:        " << options.db_name << '\n'
            << "queries:   " << options.query_base << '\n'
            << "out:       " << options.out_base << '\n'
            << "sizes:     ";
  for (size_t i = 0; i < options.sizes.size(); ++i) {
    if (i != 0) std::cout << ',';
    std::cout << options.sizes[i];
  }
  std::cout << "\nmethod:    " << options.method_name << " topk=" << options.topk
            << " metric=" << EmbeddingGraph::MetricName(options.metric) << '\n'
            << "parallel:  1 (preload once, fork per query)\n"
            << "limit:     " << options.time_limit_sec << "s/query after preload\n";
  if (options.mem_limit_gib > 0) {
    std::cout << "mem cap:   " << options.mem_limit_gib
              << " GiB/query process VmRSS\n";
  } else {
    std::cout << "mem cap:   " << options.mem_limit_pct << "% system used\n";
  }
  std::cout << "run id:    " << options.run_id << std::endl;

  if (options.mem_limit_gib <= 0 && !WaitForMemory(options.mem_limit_pct)) return 143;

  const std::chrono::steady_clock::time_point db_begin = std::chrono::steady_clock::now();
  Database database(options.db_name);
  if (!database.load()) {
    std::cerr << "database load failed: " << options.db_name << std::endl;
    return 1;
  }
  const long db_load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - db_begin).count();
  std::cout << "[preload] database loaded once, used " << db_load_ms << "ms" << std::endl;

  const EmbeddingGraph::Format format = options.pick ? EmbeddingGraph::Format::kPick
                                                      : EmbeddingGraph::Format::kRaw;
  const std::string data_dir = options.db_name + ".db/EmbeddingGraph";
  std::shared_ptr<EmbeddingGraph::ScoreContext> score_context(
      new EmbeddingGraph::ScoreContext());
  const std::chrono::steady_clock::time_point embedding_begin = std::chrono::steady_clock::now();
  if (!score_context->LoadData(data_dir, format, options.metric)) {
    std::cerr << "data embedding preload failed: " << score_context->LastError() << std::endl;
    return 1;
  }
  const long embedding_load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - embedding_begin).count();
  std::cout << "[preload] KG embeddings and name tables loaded once from " << data_dir
            << ", used " << embedding_load_ms << "ms" << std::endl;

  for (size_t size_index = 0; size_index < options.sizes.size(); ++size_index) {
    const int size = options.sizes[size_index];
    const std::string group = "n" + std::to_string(size);
    const std::string query_root = JoinPath(options.query_base, group);
    const std::string out_dir = JoinPath(options.out_base, group);
    const std::string log_dir = JoinPath(out_dir, "logs");
    if (!CreateDirectories(log_dir)) {
      std::cerr << "cannot create output directory: " << log_dir << std::endl;
      return 1;
    }
    const std::string group_run_id = options.run_id + "_" + group;
    const std::string progress = JoinPath(out_dir, "progress_" + group_run_id + ".tsv");
    const std::string summary = JoinPath(out_dir, "summary_" + group_run_id + ".txt");
    Counts counts;
    std::cout << "===== " << group << " START " << Timestamp() << " =====" << std::endl;

    for (int id = options.start_id; id <= options.end_id; ++id) {
      if (g_stop_requested) return 143;
      const std::string qid = FormatId(id, options.id_width);
      const std::string query_path = JoinPath(query_root, qid);
      const std::string output_path = JoinPath(out_dir, qid + ".txt");
      const std::string log_path = JoinPath(log_dir, qid + ".log");
      const std::string memstop_path = JoinPath(out_dir, qid + ".memstop");
      const std::string oom_path = JoinPath(out_dir, qid + ".oom");
      const std::string tmp_output = JoinPath(
          out_dir, "." + qid + "." + group_run_id + "." + std::to_string(getpid()) + ".tmp");

      if (FileNonEmpty(output_path)) {
        AppendLine(progress, "SKIP\t" + qid + "\texists\t" + Timestamp());
        ++counts.skipped;
        continue;
      }
      if (!IsDirectory(query_path)) {
        AppendLine(progress, "FAIL\t" + qid + "\tmissing_query_dir\t" + Timestamp());
        AppendLog(log_path, "missing query dir: " + query_path);
        ++counts.failed;
        continue;
      }
      if (FileExists(memstop_path)) unlink(memstop_path.c_str());
      if (FileExists(oom_path)) unlink(oom_path.c_str());
      unlink(tmp_output.c_str());
      if (options.mem_limit_gib <= 0 && !WaitForMemory(options.mem_limit_pct)) return 143;

      AppendLine(progress, "START\t" + qid + "\t\t" + Timestamp());
      pid_t child = StartChild(&database, score_context, options, method,
                               query_path, tmp_output, log_path);
      if (child < 0) {
        AppendLine(progress, "FAIL\t" + qid + "\tfork_failed\t" + Timestamp());
        AppendLog(log_path, "fork failed: " + std::string(strerror(errno)));
        ++counts.failed;
        continue;
      }
      g_active_child = child;
      WaitResult wait_result = WaitForChild(child, options);
      g_active_child = -1;

      if (wait_result.reason == StopReason::kShutdown) {
        AppendLog(log_path, "query " + qid + ": stopped by batch shutdown");
        unlink(tmp_output.c_str());
        return 143;
      }
      if (wait_result.reason == StopReason::kTimeout) {
        AppendLine(progress, "TIMEOUT\t" + qid + "\tlimit_" +
                   std::to_string(options.time_limit_sec) + "s\t" + Timestamp());
        AppendLog(log_path, "query " + qid + ": TIMEOUT after " +
                  std::to_string(options.time_limit_sec) + "s (global preload excluded)");
        unlink(tmp_output.c_str());
        ++counts.timeout;
        continue;
      }
      if (wait_result.reason == StopReason::kMemory) {
        std::ofstream marker(memstop_path.c_str(), std::ios::trunc);
        marker << "status=memstop\nquery=" << qid
               << "\nused_pct=" << wait_result.used_pct
               << "\nmem_limit_pct=" << options.mem_limit_pct
               << "\ncreated_at=" << Timestamp() << '\n';
        AppendLine(progress, "MEMSTOP\t" + qid + "\tused_" +
                   std::to_string(wait_result.used_pct) + "pct\t" + Timestamp());
        AppendLog(log_path, "query " + qid + ": MEMSTOP used=" +
                  std::to_string(wait_result.used_pct) + "% >= " +
                  std::to_string(options.mem_limit_pct) + "%");
        unlink(tmp_output.c_str());
        ++counts.memstop;
        continue;
      }
      if (wait_result.reason == StopReason::kOom) {
        std::ofstream marker(oom_path.c_str(), std::ios::trunc);
        marker << "status=oom\nquery=" << qid
               << "\nrss_kib=" << wait_result.rss_kib
               << "\nmem_limit_gib=" << options.mem_limit_gib
               << "\nmemory_metric=VmRSS"
               << "\ncreated_at=" << Timestamp() << '\n';
        AppendLine(progress, "OOM\t" + qid + "\trss_" +
                   std::to_string(wait_result.rss_kib) + "kib_limit_" +
                   std::to_string(options.mem_limit_gib) + "gib\t" + Timestamp());
        AppendLog(log_path, "query " + qid + ": OOM VmRSS=" +
                  std::to_string(wait_result.rss_kib) + " KiB >= " +
                  std::to_string(options.mem_limit_gib) + " GiB");
        unlink(tmp_output.c_str());
        ++counts.oom;
        continue;
      }

      const int rc = ExitCode(wait_result);
      if (rc == 0 && FileExists(tmp_output)) {
        if (rename(tmp_output.c_str(), output_path.c_str()) != 0) {
          AppendLine(progress, "FAIL\t" + qid + "\trename_failed\t" + Timestamp());
          AppendLog(log_path, "rename failed: " + std::string(strerror(errno)));
          ++counts.failed;
        } else {
          AppendLine(progress, "DONE\t" + qid + "\tok\t" + Timestamp());
          ++counts.done;
        }
      } else {
        AppendLine(progress, "FAIL\t" + qid + "\trc_" + std::to_string(rc) +
                   "\t" + Timestamp());
        AppendLog(log_path, "query " + qid + ": FAIL rc=" + std::to_string(rc));
        unlink(tmp_output.c_str());
        ++counts.failed;
      }
    }

    WriteSummary(summary, options, size, counts, progress);
    std::cout << "===== " << group << " END " << Timestamp()
              << " done=" << counts.done << " skip=" << counts.skipped
              << " timeout=" << counts.timeout << " fail=" << counts.failed
              << " memstop=" << counts.memstop << " oom=" << counts.oom
              << " =====" << std::endl;
  }

  TopKUtil::SetEmbeddingScoreContext(nullptr);
  std::cout << "===== persistent PTEB batch ALL DONE " << Timestamp() << " =====" << std::endl;
  return 0;
}
