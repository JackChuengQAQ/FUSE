
#ifndef TOPK_DPB_DYNAMICTRIE_H_
#define TOPK_DPB_DYNAMICTRIE_H_
#include "../../../Util/Util.h"
#include "Pool.h"
















struct TrieEntry {
  std::vector<TrieEntry*> nexts;
  
  size_t count;
  TrieEntry(size_t default_k):nexts(default_k, nullptr),count(-1){};
};

class DynamicTrie {
 private:
  TrieEntry* root;
  int depth_;
  int default_k_;
 public:
  TrieEntry* newEntry(int k);
  explicit DynamicTrie(int depth,int k );
  ~DynamicTrie();
  void deleteEntry(TrieEntry *trie_entry,int depth);
  TrieEntry* insert(const sequence &seq);
  bool detect(const sequence &seq);
};

#endif 
