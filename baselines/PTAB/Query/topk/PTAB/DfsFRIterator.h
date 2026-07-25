
#ifndef PRIVATETOPK_QUERY_TOPK_Dfs_DfsFRITERATOR_H_
#define PRIVATETOPK_QUERY_TOPK_Dfs_DfsFRITERATOR_H_

#include "./DfsList.h"
#include "./HopIndex.h"

struct DfsFrElement{
  TYPE_ENTITY_LITERAL_ID node;
  unsigned int index;
  DfsValue value_type;
  double cost;
  std::shared_ptr<DfsList> fq_;
  OnePointPredicatePtr predicates_;
  bool operator<(const DfsFrElement &other) const {
    if(this->cost < other.cost)
      return true;
    if(this->cost == other.cost)
      return this->value_type==DfsValue::Exact;
    return false;
  }
  bool operator!=(const DfsFrElement &other) const {
    return this->node!=other.node || this->value_type!= other.value_type|| this->cost!=other.cost || this->index !=other.index;
  }
};

class DfsFRIterator: public DfsList {
 private:
  
  minmax::MinMaxHeap<DfsFrElement> queue_;
  
  std::vector<std::shared_ptr<DfsList>> fqs_;
  std::vector<OnePointPredicatePtr> predicates_vec;
  std::shared_ptr<IDListWithAppending> unexplored_fqs_;
 public:
  DfsFRIterator();
  virtual ~DfsFRIterator(){};
  OrderedListType Type() override {return OrderedListType::FR;};
  bool Empty() {
    if(!this->pool_.empty())
      return false;
    if(!this->queue_.empty())
      return false;
    return this->unexplored_fqs_->Empty(); };
  void TryGetNext(DfsHelpInfo* dfs_help_info) override;
  
  void Insert(TYPE_ENTITY_LITERAL_ID fq_id,
              std::shared_ptr<DfsList> fq_pointer,
              OnePointPredicatePtr predicates_vec);

  void SetUnexploredFqInfo(  std::shared_ptr<IDListWithAppending> unexplored_fqs)
  {
    this->unexplored_fqs_ = std::move(unexplored_fqs);
  }
  inline void SetNotFullyExplored(bool v){this->not_fully_explored_=v;}
  void GetResult(int i_th,std::shared_ptr<std::vector<TYPE_ENTITY_LITERAL_ID>> record,
                         NodeOneChildVarPredicatesPtr predicate_information = nullptr) override;
  void Estimate() override;
  





  void ExploreAllDescendents(DfsHelpInfo* dfs_help_info);
};



#endif 
