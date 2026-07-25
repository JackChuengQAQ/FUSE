
#include "DfsFQIterator.h"
#include "DfsFRIterator.h"
#include "DfsOWIterator.h"

void DfsFQIterator::TryGetNext(DfsHelpInfo *dfs_help_info)
{
  bool embedding_score_enabled =
      dfs_help_info != nullptr && dfs_help_info->env != nullptr &&
      dfs_help_info->env->embedding_score_context != nullptr &&
      dfs_help_info->env->embedding_score_context->Enabled();
  
  if (this->not_fully_explored_) {
    double cost = this->node_score_;
    for(auto& fr_ow_it:this->fr_ow_iterators_) {
      fr_ow_it->TryGetNext(dfs_help_info);
      if(fr_ow_it->pool_.empty())
        return;
       cost += fr_ow_it->pool_[0].cost;
    }
    
    auto e_seq = sequence(fr_ow_iterators_.size(), 0);
    
    this->compressed_vector_.Insert(e_seq);
    this->queue_.push(DfsFqElement(std::move(e_seq),DfsValue::Exact,cost));
    this->not_fully_explored_ = false;
  }

  while (!this->queue_.empty()) {
    auto top_element = this->queue_.popMin();
    auto type_value = top_element.value_type;
    if (type_value == DfsValue::Range) {
      bool success_change = true;
      double cost = this->node_score_;
      
      for (unsigned int i = 0;i<this->fr_ow_iterators_.size();i++) {
        auto &fr_ow_it = this->fr_ow_iterators_[i];
        auto seq_id = top_element.seq[i];
        
        if (fr_ow_it->pool_.size() <= seq_id)
          fr_ow_it->TryGetNext(dfs_help_info);
        
        if (fr_ow_it->pool_.size() <= seq_id) {
          success_change = false;
          break;
        }
        else
          cost += fr_ow_it->pool_[seq_id].cost;
      }



      if(!success_change)
        continue;
      top_element.cost = cost;
      top_element.value_type = DfsValue::Exact;
      this->queue_.push(std::move(top_element));
    } else if(type_value == DfsValue::Exact) {
      auto cost = top_element.cost;
      this->e_pool_.push_back(FqElement(top_element.seq,cost));
      this->pool_.push_back(element{0,static_cast<unsigned int>(this->pool_.size()-1),cost});
      
      auto &seq = top_element.seq;
      this->compressed_vector_.Insert(seq);
      for(unsigned int j=0; j<this->fr_ow_iterators_.size(); j++) {
        seq[j] += 1;
        
        if(this->compressed_vector_.AllParentsInserted(seq)) {
          auto& fr_ow_it = this->fr_ow_iterators_[j];
          if(fr_ow_it->pool_.size() == seq[j] && !fr_ow_it->Exhausted()) {
            if (embedding_score_enabled) {
              
              
              fr_ow_it->TryGetNext(dfs_help_info);
              if(fr_ow_it->pool_.size() > seq[j]) {
                auto cost = top_element.cost - fr_ow_it->pool_[seq[j] - 1].cost + fr_ow_it->pool_[seq[j]].cost;
                this->queue_.push(DfsFqElement(seq,DfsValue::Exact,cost));
              }
            } else {
              auto estimated_cost =  top_element.cost - fr_ow_it->pool_[seq[j] - 1].cost;
              
              if(fr_ow_it->Type() == OrderedListType::FR)
              {
                fr_ow_it->Estimate();
                estimated_cost += fr_ow_it->estimate_[0];
                this->queue_.push(DfsFqElement(seq,DfsValue::Range,estimated_cost));
              }
            }
          }
          else if(fr_ow_it->pool_.size() > seq[j]) {
            auto cost = top_element.cost - fr_ow_it->pool_[seq[j] - 1].cost + fr_ow_it->pool_[seq[j]].cost;
            this->queue_.push(DfsFqElement(seq,DfsValue::Exact,cost));
          }
        }
        seq[j] -= 1;
      }
      
      if(this->queue_.empty())
        this->SetExhausted(true);
      else
        this->estimate_[0] = this->queue_.findMin().cost;
      
      return;
    }
    else
      throw string("wrong DfsValue in DfsFQIterator::TryGetNext");
  }
}

void DfsFQIterator::Insert(std::shared_ptr<DfsList> FR_OW_iterator) {
  this->fr_ow_iterators_.push_back(std::move(FR_OW_iterator));
}







void DfsFQIterator::Insert(std::vector<std::shared_ptr<DfsList>> FR_OW_iterators) {
  this->fr_ow_iterators_ = std::move(FR_OW_iterators);
}

void DfsFQIterator::GetResult(int i_th, std::shared_ptr<std::vector<TYPE_ENTITY_LITERAL_ID>> record,
                              NodeOneChildVarPredicatesPtr predicate_information) {
  record->push_back(this->node_id_);
  auto &seq = this->e_pool_[i_th].seq;
  for(unsigned int i =0; i<this->fr_ow_iterators_.size(); i++) {
    if(fr_ow_iterators_[i]->Type() ==OrderedListType::OW) {
      auto ow_predicates = this->types_predicates_[i];
      fr_ow_iterators_[i]->GetResult(seq[i], record,ow_predicates);
    }
    else
      fr_ow_iterators_[i]->GetResult(seq[i], record);
  }
}

void DfsFQIterator::Estimate() {
  
  if (!this->not_fully_explored_) {
    this->estimate_[0] = this->node_score_;
    for (const auto &descent_it : this->fr_ow_iterators_)
      this->estimate_[0] += descent_it->estimate_[0];
    return;
  }

  
  for (unsigned int i = 0; i < D_HOP_LOG; i++)
    this->estimate_[i] = this->node_score_;

  for (const auto &descent_it : this->fr_ow_iterators_) {
    if (descent_it->Type() == OrderedListType::OW)
      continue;
    for (unsigned int i = 0; i < D_HOP_LOG - 1; i++) {
      if (descent_it->pool_.empty())
        this->estimate_[i] += descent_it->estimate_[i + 1];
      else
        this->estimate_[i] += std::min(descent_it->estimate_[i + 1], descent_it->pool_[0].cost);
    }
  }
  for (const auto &descent_it : this->fr_ow_iterators_) {
    if (descent_it->Type() != OrderedListType::OW)
      continue;
    for (unsigned int i = 0; i < D_HOP_LOG; i++) {
      if (descent_it->pool_.empty())
        this->estimate_[i] += descent_it->estimate_[i];
      else
        this->estimate_[i] += std::min(descent_it->estimate_[i], descent_it->pool_[0].cost);
    }
  }
}
