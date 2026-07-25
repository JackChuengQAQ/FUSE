#ifndef FUSE_VAMANA_SEARCH_QUEUE_H
#define FUSE_VAMANA_SEARCH_QUEUE_H

#include <algorithm>
#include <cstring>
#include <vector>

#include "configuration/types.h"



struct VamanaCandidate {
    VertexID id;
    float distance;
    bool expanded;

    VamanaCandidate() : id(0), distance(0.0f), expanded(false) {
    }
    VamanaCandidate(VertexID a, float b) : id(a), distance(b), expanded(false) {
    }

    bool operator<(const VamanaCandidate& other) const {
        return distance < other.distance || (distance == other.distance && id < other.id);
    }
};

class VamanaSearchQueue {
public:
    VamanaSearchQueue() : size_(0), capacity_(0), cur_unexpanded_(0) {
    }

    int32_t size() const {
        return size_;
    }

    void reserve(int32_t capacity) {
        if (capacity + 1 > static_cast<int32_t>(data_.size())) {
            data_.resize(static_cast<size_t>(capacity + 1));
        }
        capacity_ = capacity;
    }

    void clear() {
        size_ = 0;
        cur_unexpanded_ = 0;
    }

    bool exist(VertexID id) const {
        for (int32_t i = 0; i < size_; ++i) {
            if (data_[static_cast<size_t>(i)].id == id) {
                return true;
            }
        }
        return false;
    }

    void insert(VertexID id, float distance) {
        VamanaCandidate new_candidate(id, distance);
        if (size_ == capacity_ && data_[static_cast<size_t>(size_ - 1)] < new_candidate) {
            return;
        }

        int32_t lo = 0;
        int32_t hi = size_;
        while (lo < hi) {
            int32_t mid = (lo + hi) >> 1;
            if (new_candidate < data_[static_cast<size_t>(mid)]) {
                hi = mid;
            } else if (data_[static_cast<size_t>(mid)].id == new_candidate.id) {
                return;
            } else {
                lo = mid + 1;
            }
        }

        if (lo < capacity_) {
            std::memmove(&data_[static_cast<size_t>(lo + 1)],
                         &data_[static_cast<size_t>(lo)],
                         static_cast<size_t>(size_ - lo) * sizeof(VamanaCandidate));
        }
        data_[static_cast<size_t>(lo)] = new_candidate;

        if (size_ < capacity_) {
            ++size_;
        }
        if (lo < cur_unexpanded_) {
            cur_unexpanded_ = lo;
        }
    }

    bool hasUnexpandedNode() const {
        return cur_unexpanded_ < size_;
    }

    const VamanaCandidate& getClosestUnexpanded() {
        data_[static_cast<size_t>(cur_unexpanded_)].expanded = true;
        int32_t pre = cur_unexpanded_;
        while (cur_unexpanded_ < size_ && data_[static_cast<size_t>(cur_unexpanded_)].expanded) {
            ++cur_unexpanded_;
        }
        return data_[static_cast<size_t>(pre)];
    }

    VamanaCandidate operator[](int32_t idx) const {
        return data_[static_cast<size_t>(idx)];
    }

private:
    int32_t size_;
    int32_t capacity_;
    int32_t cur_unexpanded_;
    std::vector<VamanaCandidate> data_;
};

class VamanaVisitedSet {
public:
    void init(ui num_elements) {
        cur_value_ = 0;
        num_elements_ = num_elements;
        marks_.assign(num_elements, 0);
    }

    void clear() {
        ++cur_value_;
        if (cur_value_ == 0) {
            std::fill(marks_.begin(), marks_.end(), static_cast<uint16_t>(0));
            cur_value_ = 1;
        }
    }

    void set(VertexID idx) {
        if (idx < marks_.size()) {
            marks_[idx] = cur_value_;
        }
    }

    bool check(VertexID idx) const {
        return idx < marks_.size() && marks_[idx] == cur_value_;
    }

private:
    uint16_t cur_value_ = 0;
    std::vector<uint16_t> marks_;
    ui num_elements_ = 0;
};

struct VamanaSearchScratch {
    VamanaSearchQueue search_queue;
    VamanaVisitedSet visited_set;
    std::vector<VamanaCandidate> expanded_list;
    std::vector<float> occlude_factor;

    explicit VamanaSearchScratch(ui num_vertices, int32_t queue_capacity)
        : search_queue(), visited_set(), expanded_list(), occlude_factor() {
        search_queue.reserve(queue_capacity);
        visited_set.init(num_vertices);
    }
};

#endif
