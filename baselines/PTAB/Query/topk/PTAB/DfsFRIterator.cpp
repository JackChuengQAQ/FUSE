
#include "DfsFQIterator.h"
#include "DfsFRIterator.h"
#include "DfsOWIterator.h"
#include "DfsUtil.h"
#include "DfsUtilDynamic.h"





void DfsFRIterator::TryGetNext(DfsHelpInfo* dfs_help_info)
{
  bool embedding_score_enabled =
      dfs_help_info != nullptr && dfs_help_info->env != nullptr &&
      dfs_help_info->env->embedding_score_context != nullptr &&
      dfs_help_info->env->embedding_score_context->Enabled();
  if(this->not_fully_explored_)
  {
    this->ExploreAllDescendents(dfs_help_info);
    this->not_fully_explored_ = false;
  }

  
  while(!queue_.empty())
  {
    auto min_e = this->queue_.popMin();
    auto node_id = min_e.node;
    auto index = min_e.index;
    
    auto &fq = min_e.fq_;
    auto &predicates = min_e.predicates_;
    
    if(min_e.value_type==DfsValue::Exact)
    {
      this->pool_.push_back(element{.node = node_id, .index = index, .cost = min_e.cost});
      this->fqs_.push_back(min_e.fq_);
      this->predicates_vec.push_back(min_e.predicates_);
      auto next_index = index + 1;
      
      if(fq->pool_.size() > next_index)
      {
        this->queue_.push(DfsFrElement{.node=node_id,
            .index= next_index,
            .value_type=DfsValue::Exact,
            .cost = fq->pool_[next_index].cost,
            .fq_ = fq,
            .predicates_ = predicates});
      }
      else if(fq->pool_.size() == next_index &&  !fq->Exhausted())
      {
        if (embedding_score_enabled) {
          
          
          fq->TryGetNext(dfs_help_info);
          if (fq->pool_.size() > next_index) {
            this->queue_.push(DfsFrElement{.node=node_id,
                .index=next_index,
                .value_type=DfsValue::Exact,
                .cost = fq->pool_[next_index].cost,
                .fq_ = fq,
                .predicates_ = predicates});
          }
        } else {
          this->queue_.push(DfsFrElement{.node=node_id,
              .index=next_index,
              .value_type=DfsValue::Range,
              .cost = fq->estimate_[0],
              .fq_ = fq,
              .predicates_ = predicates});
        }
      }
      if(this->queue_.empty())
        this->SetExhausted(true);
      return;
    }
    
    if (fq->pool_.size() < index + 1)
      fq->TryGetNext(dfs_help_info);
    
    if(fq->pool_.size() >= index + 1)
    {
      this->queue_.push(DfsFrElement{.node=node_id,
          .index=index,
          .value_type=DfsValue::Exact,
          .cost = fq->pool_[index].cost,
          .fq_ = fq,
          .predicates_ = predicates});
    }
  }

  if(this->queue_.empty())
    this->SetExhausted(true);
  else 
    this->estimate_[0] = this->queue_.findMin().cost;
  
  return;

}






void DfsFRIterator::Insert(TYPE_ENTITY_LITERAL_ID fq_id,
                           std::shared_ptr<DfsList> fq_pointer,
                           OnePointPredicatePtr predicates_vec) {
  
  DfsFrElement e{.node=fq_id, .index=0};
  if (fq_pointer->pool_.empty()) {
    e.cost = fq_pointer->estimate_[0];
    e.value_type = DfsValue::Range;
  } else {
    e.cost = fq_pointer->pool_[0].cost;
    e.value_type = DfsValue::Exact;
  }
  e.fq_ = std::move(fq_pointer);
  e.predicates_ = std::move(predicates_vec);
  
  queue_.push(e);
}







void DfsFRIterator::GetResult(int i_th, std::shared_ptr<std::vector<TYPE_ENTITY_LITERAL_ID>> record,
                              NodeOneChildVarPredicatesPtr predicate_information) {
  auto fq = this->pool_[i_th];
  auto fq_i_th = fq.index;
  auto fq_id = fq.node;
  auto fq_pointer = this->fqs_[i_th];
  
  if(this->predicates_vec[i_th] != nullptr)
    for (auto pre_id:*this->predicates_vec[i_th])
      record->push_back(pre_id);





  fq_pointer->GetResult(fq_i_th,record);
}


DfsFRIterator::DfsFRIterator() {
  
}

void DfsFRIterator::Estimate() {
  
  if(this->has_estimated_)
    return;
  const auto & min_e = this->queue_.findMin();
  
  auto &min_fq = min_e.fq_;
  min_fq->Estimate();
  std::copy_n(min_fq->estimate_, D_HOP_LOG, this->estimate_);
}


void DfsFRIterator::ExploreAllDescendents(DfsHelpInfo* dfs_help_info) {
  int fq_var_id;
  
  if(!this->queue_.empty())
  {
    fq_var_id = this->queue_.findMin().fq_->GetVarId();
  }
  else if(!this->fqs_.empty())
    fq_var_id = this->fqs_[0]->GetVarId();
  else
    return;

  auto fq_it = this->unexplored_fqs_->Begin();
  while(fq_it !=  this->unexplored_fqs_->End()) {
    auto chosen_id_to_explore = fq_it->first;
    size_t useless_distinct_level = 0;
    size_t useless_uncertain_level = 0;
    if(dfs_help_info->DfsNew)
    {
      auto fq = DfsUtilCompressedVector::ExploreFQ(chosen_id_to_explore,
                                                   fq_var_id,
                                                   useless_distinct_level,
                                                   useless_uncertain_level,
                                                   dfs_help_info);
      if (fq != nullptr)
        this->Insert(chosen_id_to_explore, fq, fq_it->second);

    }
    else {
      auto fq = DfsUtilDynamic::ExploreFQ(chosen_id_to_explore,
                                          fq_var_id,
                                          useless_distinct_level,
                                          useless_uncertain_level,
                                          dfs_help_info);
      if (fq != nullptr)
        this->Insert(chosen_id_to_explore, fq, fq_it->second);
    }
    fq_it = this->unexplored_fqs_->Erase(fq_it);
  }
  this->Estimate();
}
