
#ifndef PRIVATETOPK_QUERY_TOPK_Dfs_DfsOWITERATOR_H_
#define PRIVATETOPK_QUERY_TOPK_Dfs_DfsOWITERATOR_H_

#include "./DfsList.h"
#include "./HopIndex.h"
class DfsOWIterator: public DfsList{
 private:
  
  
 public:
  DfsOWIterator()=default;
  virtual ~DfsOWIterator(){};
  OrderedListType Type() override {return OrderedListType::OW;};
  bool Empty() {
    return this->pool_.empty();
  };
  void TryGetNext(DfsHelpInfo *dfs_help_info);
  
  void Insert(unsigned int k, const std::vector<TYPE_ENTITY_LITERAL_ID>& ids, const std::vector<double>& scores);
  void Insert(unsigned int k,const std::vector<TYPE_ENTITY_LITERAL_ID> &ids);
  virtual void GetResult(int i_th,std::shared_ptr<std::vector<TYPE_ENTITY_LITERAL_ID>> record,
                         NodeOneChildVarPredicatesPtr predicate_information = nullptr) override;
  void Estimate() override { return; };
};

#endif 
