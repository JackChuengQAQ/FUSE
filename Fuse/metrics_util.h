#ifndef FUSE_METRICS_UTIL_H
#define FUSE_METRICS_UTIL_H

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include "configuration/types.h"





inline double readVmKbField(const char* field) {
    
    
    
    std::ifstream f("/proc/self/status");
    if (!f.is_open()) {
        return -1.0;
    }
    const std::string prefix = std::string(field) + ":";
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < prefix.size()) {
            continue;
        }
        if (line.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        
        size_t i = prefix.size();
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        size_t j = i;
        while (j < line.size() && line[j] >= '0' && line[j] <= '9') {
            ++j;
        }
        if (j == i) {
            return -1.0;
        }
        const std::string num = line.substr(i, j - i);
        long long kb = std::strtoll(num.c_str(), NULL, 10);
        return static_cast<double>(kb) / 1024.0;
    }
    return -1.0;
}

inline double readVmRssMb()  { return readVmKbField("VmRSS"); }
inline double readVmPeakMb() { return readVmKbField("VmPeak"); }




struct CandStats {
    uint64_t sum = 0;
    uint64_t cover_sum = 0;
    uint64_t independent_sum = 0;
    uint64_t max = 0;
    uint64_t min = 0;
    double mean = 0.0;
    std::vector<uint64_t> per_node;  
    bool valid = false;              
};

inline CandStats summarizeCandidates(
    const std::vector<std::vector<VertexID> >& cands,
    const std::unordered_set<VertexID>& cover_set) {
    CandStats s;
    if (cands.empty()) {
        return s;
    }
    s.valid = true;
    s.per_node.resize(cands.size(), 0);
    s.min = std::numeric_limits<uint64_t>::max();
    for (size_t u = 0; u < cands.size(); ++u) {
        const uint64_t c = static_cast<uint64_t>(cands[u].size());
        s.per_node[u] = c;
        s.sum += c;
        if (c > s.max) s.max = c;
        if (c < s.min) s.min = c;
        if (cover_set.count(static_cast<VertexID>(u))) {
            s.cover_sum += c;
        } else {
            s.independent_sum += c;
        }
    }
    if (s.min == std::numeric_limits<uint64_t>::max()) {
        s.min = 0;
    }
    s.mean = cands.empty() ? 0.0 : static_cast<double>(s.sum) / static_cast<double>(cands.size());
    return s;
}

#endif
