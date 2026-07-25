
#include "DynamicTrie.h"






TrieEntry* DynamicTrie::newEntry(int k) {
  return new TrieEntry(k);
}













DynamicTrie::DynamicTrie(int depth,int k ): depth_(depth), default_k_(k) {
  this->root = newEntry(k);
}






void DynamicTrie::deleteEntry(TrieEntry *trie_entry, int depth) {
  for(unsigned int i=0;i<trie_entry->nexts.size();i++)
    if(trie_entry->nexts[i] != nullptr)
      deleteEntry(trie_entry->nexts[i],depth+1);
  delete trie_entry;
}

DynamicTrie::~DynamicTrie() {
  
  deleteEntry(this->root,0);
}









TrieEntry* DynamicTrie::insert(const sequence &seq) {
  TrieEntry* p = this->root;
  TrieEntry* p_next;
  bool new_created = false;
  for(int i=0;i<depth_;i++)
  {
    auto i_th_element = seq[i];

    
    while(i_th_element>=p->nexts.size())
    {
      p->nexts.push_back(nullptr);
    }

    p_next = p->nexts[seq[i]];
    if(p_next == nullptr) {
      p_next = newEntry(this->default_k_);
      p->nexts[seq[i]] = p_next;
      new_created = true;
    }
    p = p_next;
  }
  if(new_created) {
    auto father_count = static_cast<size_t>(std::count_if(seq.begin(),
                                     seq.end(),
                                     [](decltype(*seq.begin()) x) { return x != 0; })
    );
    p_next->count = father_count;
  }
  return p_next;
}







bool DynamicTrie::detect(const sequence &seq) {
  auto p_next = this->insert(seq);
  p_next->count-- ;
  return p_next->count==0;
}


