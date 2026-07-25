#ifndef PTAB_MIN_MAX_HEAP_HPP
#define PTAB_MIN_MAX_HEAP_HPP

#include <functional>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace minmax {

template <class T, class Container = std::vector<T>, class Compare = std::less<T>>
class MinMaxHeap {
public:
    MinMaxHeap() = default;

    explicit MinMaxHeap(Container values, Compare compare = Compare())
        : values_(compare) {
        values_.insert(std::make_move_iterator(values.begin()),
                       std::make_move_iterator(values.end()));
    }

    bool empty() const {
        return values_.empty();
    }

    unsigned int size() const {
        return static_cast<unsigned int>(values_.size());
    }

    void push(const T& value) {
        values_.insert(value);
    }

    void push(T&& value) {
        values_.insert(std::move(value));
    }

    const T& findMin() const {
        if (values_.empty()) {
            throw std::underflow_error("minimum requested from an empty heap");
        }
        return *values_.begin();
    }

    const T& findMax() const {
        if (values_.empty()) {
            throw std::underflow_error("maximum requested from an empty heap");
        }
        return *std::prev(values_.end());
    }

    T popMin() {
        if (values_.empty()) {
            throw std::underflow_error("minimum requested from an empty heap");
        }
        const auto position = values_.begin();
        auto node = values_.extract(position);
        return std::move(node.value());
    }

    T popMax() {
        if (values_.empty()) {
            throw std::underflow_error("maximum requested from an empty heap");
        }
        const auto position = std::prev(values_.end());
        auto node = values_.extract(position);
        return std::move(node.value());
    }

    T pop() {
        return popMax();
    }

private:
    std::multiset<T, Compare> values_;
};

}

#endif
