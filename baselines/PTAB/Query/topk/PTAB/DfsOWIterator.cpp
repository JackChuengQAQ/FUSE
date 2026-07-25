
#include "DfsFQIterator.h"
#include "DfsOWIterator.h"

void DfsOWIterator::TryGetNext(DfsHelpInfo *dfs_help_info) {



  return;
}

void DfsOWIterator::Insert(unsigned int k,
                           const std::vector<TYPE_ENTITY_LITERAL_ID> &ids,
                           const std::vector<double> &scores) {
  struct ScorePair {
    TYPE_ENTITY_LITERAL_ID id;
    double cost;
    bool operator<(const ScorePair &other) const { return this->cost < other.cost; };
  };
  std::vector<ScorePair> ranks;
  this->pool_.reserve(k);
  ranks.reserve(ids.size());
  for (unsigned int i = 0; i < ids.size(); i++)
    ranks.push_back(ScorePair{ids[i], scores[i]});
  std::sort(ranks.begin(), ranks.end());
  for (unsigned int i = 0; i < k && i < ranks.size(); i++) {
    element e{};
    e.index = i;
    e.cost = ranks[i].cost;
    e.node = ranks[i].id;
    this->pool_.push_back(e);
  }
  this->SetExhausted(true);
}





void DfsOWIterator::Insert(unsigned int k, const std::vector<TYPE_ENTITY_LITERAL_ID> &ids) {
  pool_.reserve(k);
  for (unsigned int i = 0; i < k && i < ids.size(); i++) {
    element e{};
    e.index = i;
    e.cost = 0.0;
    e.node = ids[i];
    this->pool_.push_back(e);
  }
  this->SetExhausted(true);
}

void DfsOWIterator::GetResult(int i_th, std::shared_ptr<std::vector<TYPE_ENTITY_LITERAL_ID>> record,
                              NodeOneChildVarPredicatesPtr predicate_information) {
  auto &i_th_element = this->pool_[i_th];
  auto node_id = i_th_element.node;
  auto predicates = (*predicate_information)[node_id];
  for(auto predicate_id:*predicates)
    record->push_back(predicate_id);
  record->push_back(node_id);
}