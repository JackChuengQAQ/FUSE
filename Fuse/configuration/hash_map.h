#ifndef FUSE_HASH_MAP_H
#define FUSE_HASH_MAP_H

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

template <class Key, class Value, class Hash = std::hash<Key>,
          class Equal = std::equal_to<Key>,
          class Allocator = std::allocator<std::pair<const Key, Value>>>
using sparse_hash_map = std::unordered_map<Key, Value, Hash, Equal, Allocator>;

#endif
