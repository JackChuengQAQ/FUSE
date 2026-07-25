
#include "DfsUtil.h"





DfsFRIterator *DfsUtilCompressedVector::BuildIteratorTree(const shared_ptr<TopKSearchPlan> tree_search_plan,
                                                          DfsHelpInfo *dfs_help_info) {

  auto root_plan = tree_search_plan->tree_root_;
  auto root_var = root_plan->var_id;

  
  auto root_candidate_ids = (*dfs_help_info->id_caches)[root_var];

  set<TYPE_ENTITY_LITERAL_ID> root_candidate;

  bool embedding_score_enabled =
      dfs_help_info->env != nullptr && dfs_help_info->env->embedding_score_context != nullptr &&
      dfs_help_info->env->embedding_score_context->Enabled();
  auto coefficient_it = dfs_help_info->coefficients->find(dfs_help_info->bgp_query->get_var_name_by_id((root_var)));
  bool has_coefficient = coefficient_it != dfs_help_info->coefficients->end();
  double coefficient = has_coefficient? (*coefficient_it).second:0.0;
  
  shared_ptr<std::map<TYPE_ENTITY_LITERAL_ID, double>> node_score = nullptr;

  
  for (auto root_id : *root_candidate_ids->getList()) {
    root_candidate.insert(root_id);
  }
#ifdef TOPK_DEBUG_INFO
  cout<<"ROOT has"<< root_candidate.size()<<" ids"<<endl;
#endif

  if(embedding_score_enabled && has_coefficient)
    node_score = GetChildNodeScores(coefficient,root_candidate, false, nullptr, nullptr,
                                    nullptr,dfs_help_info->env, root_var);
#ifdef FQ_COEFFICIENT
  else if(has_coefficient)
    node_score = GetChildNodeScores(coefficient,root_candidate, false, nullptr, nullptr,
                                    nullptr,dfs_help_info->env, root_var);
#endif

#ifdef ITERATOR_COUNT
  search_node_count += root_candidate.size();
  cout << "PTAB, search_node_count = " << search_node_count << endl;
#endif

  
  std::vector<std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFRIterator>>> descendents_FRs;

  std::vector<std::map<TYPE_ENTITY_LITERAL_ID, 
                       std::pair<std::shared_ptr<DfsOWIterator>, 
                                 NodeOneChildVarPredicatesPtr>>> 
  descendents_OWs;

  descendents_FRs.reserve(root_plan->descendents_fr_num_);
  descendents_OWs.reserve(root_plan->descendents_ow_num_);

  std::set<TYPE_ENTITY_LITERAL_ID> deleted_root_ids;
  unsigned descendents_num = root_plan->descendents_.size();

  for (unsigned descendent_i = 0; descendent_i < descendents_num; descendent_i++) {
    auto descendent_plan = root_plan->descendents_[descendent_i];
    auto descendent_tree_edges_plan = root_plan->tree_edges_[descendent_i];
    auto is_fr = !descendent_plan->descendents_.empty();

    if (is_fr) {
      
      if (dfs_help_info->max_leaf_distance_[root_var] <= 3) {
        std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFRIterator>> descendent_iterators;
        
        size_t fq_distinct_level = 0;
        size_t fq_uncertain_level = 0;
        std::set<TYPE_ENTITY_LITERAL_ID> deleted_child_this_iterator;
        auto fq_id_it = root_candidate.begin();
        while (fq_id_it != root_candidate.end()) {
          auto fq_id = *fq_id_it;
          auto fr = ExploreFRs(root_var,
                               fq_id,
                               descendent_plan->var_id,
                               fq_distinct_level,
                               fq_uncertain_level,
                               *descendent_tree_edges_plan,
                               dfs_help_info);
          
          if (fr == nullptr || fr->Empty())
            fq_id_it = root_candidate.erase(fq_id_it);
          else {
            descendent_iterators[fq_id] = std::move(fr);
            fq_id_it++;
          }
        }
        descendents_FRs.push_back(std::move(descendent_iterators));
      } else {
        auto descendent_iterators = GenerateFRs(descendent_plan->var_id,
                                                descendent_tree_edges_plan,
                                                root_candidate,
                                                deleted_root_ids,
                                                descendent_plan,
                                                dfs_help_info);
        descendents_FRs.push_back(std::move(descendent_iterators));
      }
    } else {
      std::map<TYPE_ENTITY_LITERAL_ID, 
               std::pair<std::shared_ptr<DfsOWIterator>, 
                         NodeOneChildVarPredicatesPtr>> 
      descendent_iterators;

      auto fq_id_it = root_candidate.begin();
      while (fq_id_it != root_candidate.end()) {
        auto fq_id = *fq_id_it;
        
        size_t ow_distinct_level = 0;
        size_t ow_uncertain_level = 0;
        auto ow_pair = ExploreOW(fq_id,
                                 descendent_plan->var_id,
                                 ow_distinct_level,
                                 ow_uncertain_level,
                                 *descendent_tree_edges_plan,
                                 dfs_help_info);
        
        if (ow_pair.first == nullptr || ow_pair.first->Empty())
          fq_id_it = root_candidate.erase(fq_id_it);
        else {



          descendent_iterators[fq_id] = std::move(ow_pair);
          fq_id_it++;
        }
      }
      descendents_OWs.push_back(std::move(descendent_iterators));
    }
  }


  
  
  auto child_fqs = AssemblingFrOw(root_candidate, root_var, dfs_help_info->k,
                                  node_score, descendents_FRs, descendents_OWs);

  auto empty_pre = make_shared<OnePointPredicateVec>();
  auto fr = new DfsFRIterator();
  
  for (const auto &root_id_fq : child_fqs) {
    auto root_id = root_id_fq.first;
    auto fq_pointer = root_id_fq.second;
    if (embedding_score_enabled && fq_pointer != nullptr && fq_pointer->pool_.empty())
      fq_pointer->TryGetNext(dfs_help_info);
    fr->Insert(root_id, fq_pointer, empty_pre);
  }
  
  fr->SetNotFullyExplored(false);
#ifdef TOPK_DEBUG_INFO
  cout<<"ROOT has"<< root_candidate.size()<<"ids "<<", FR has "<<child_fqs.size()<<" FQs"<<endl;
#endif
#ifdef ITERATOR_COUNT
  FR_NUM++;
#endif
  return fr;
}






std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFQIterator>>
DfsUtilCompressedVector::AssemblingFrOw(std::set<TYPE_ENTITY_LITERAL_ID> &fq_ids,
                                        int fq_var_id, int k,
                                        std::shared_ptr<std::map<TYPE_ENTITY_LITERAL_ID, double>> &node_scores,
                                        std::vector<std::map<TYPE_ENTITY_LITERAL_ID,
                                                             std::shared_ptr<DfsFRIterator> >> &descendents_FRs,
                                        std::vector<
                                            std::map<TYPE_ENTITY_LITERAL_ID, 
                                                     std::pair<std::shared_ptr<DfsOWIterator>, 
                                                               NodeOneChildVarPredicatesPtr>>> 
                                        &descendents_OWs) {
  auto ow_size = descendents_OWs.size();
  auto fr_size = descendents_FRs.size();
  auto child_type_num = ow_size + fr_size;
  std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFQIterator>> id_fqs;
  for (auto fq_id : fq_ids) {
    double score = 0.0;
    if (node_scores != nullptr)
      score = (*node_scores)[fq_id];
    auto fq = make_shared<DfsFQIterator>(k, fq_id, fq_var_id, child_type_num, score);
    
    bool flag = true;
    for (decltype(child_type_num) i = 0; i < ow_size; i++) {
      fq->Insert(descendents_OWs[i][fq_id].first);
      fq->AddOneTypePredicate(descendents_OWs[i][fq_id].second);
    }
    for (decltype(child_type_num) i = 0; i < fr_size; i++) {
      if (!descendents_FRs[i][fq_id]) {flag = false; break;}
      fq->Insert(descendents_FRs[i][fq_id]);
    }
    if (!flag) continue;
    id_fqs[fq_id] = fq;
    fq->Estimate();
#ifdef SHOW_SCORE
    cout<<"FQ["<<fq_id<<"], the min score are "<<fq->pool_[0].cost<<endl;
#endif
#ifdef ITERATOR_COUNT
    FQ_NUM++;
#endif
  }
  return id_fqs;
}

std::shared_ptr<DfsFQIterator>
DfsUtilCompressedVector::AssemblingTheFq(TYPE_ENTITY_LITERAL_ID fq_id,
                                         double node_score, int fq_var_id, int k,
                                         std::vector<std::shared_ptr<DfsFRIterator>> &descendents_FRs,
                                         std::vector<std::pair<std::shared_ptr<DfsOWIterator>,
                                                               NodeOneChildVarPredicatesPtr>> &descendents_OWs) {
  auto ow_size = descendents_OWs.size();
  auto fr_size = descendents_FRs.size();
  auto child_type_num = ow_size + fr_size;
  
  auto fq = make_shared<DfsFQIterator>(k, fq_id, fq_var_id, child_type_num, node_score);
  
  for (decltype(child_type_num) i = 0; i < ow_size; i++) {
    fq->Insert(descendents_OWs[i].first);
    fq->AddOneTypePredicate(descendents_OWs[i].second);
  }
  for (decltype(child_type_num) i = 0; i < fr_size; i++)
    fq->Insert(descendents_FRs[i]);
  fq->Estimate();
#ifdef SHOW_SCORE
  cout<<"FQ["<<fq_id<<"], the min score are "<<fq->pool_[0].cost<<endl;
#endif
#ifdef ITERATOR_COUNT
  FQ_NUM++;
#endif

  return fq;
}

std::pair<std::shared_ptr<DfsOWIterator>, 
          NodeOneChildVarPredicatesPtr> 
DfsUtilCompressedVector::ExploreOW(TYPE_ENTITY_LITERAL_ID parent_id,
                                   int child_var,
                                   size_t &distinct_level,
                                   size_t &uncertain_level,
                                   const TopKPlanUtil::TreeEdge &tree_edges,
                                   DfsHelpInfo *dfs_help_info) {
  auto coefficient_it = dfs_help_info->coefficients->find(dfs_help_info->bgp_query->get_var_name_by_id((child_var)));
  bool has_coefficient = coefficient_it != dfs_help_info->coefficients->end();
  double coefficient = has_coefficient ? (*coefficient_it).second : 0.0;
  auto hop_index = dfs_help_info->hop_index;
  
  
  auto pre_constant = tree_edges.predicate_constant_[0];
  auto pre_id = tree_edges.predicate_ids_[0];
  bool embedding_score_enabled =
      dfs_help_info->env != nullptr && dfs_help_info->env->embedding_score_context != nullptr &&
      dfs_help_info->env->embedding_score_context->Enabled();
  bool valid_pre_to_num = !embedding_score_enabled && hop_index->IsValidPredicateToNumber(pre_id);
  if (has_coefficient && tree_edges.predicate_ids_.size() == 1 && pre_constant &&
      tree_edges.directions_[0] == TopKPlanUtil::EdgeDirection::OUT && valid_pre_to_num) {
    
    
    auto pre_pos = hop_index->GetOffset(parent_id, pre_id);
    if (pre_pos == static_cast<size_t>(-1))
      return make_pair(std::shared_ptr<DfsOWIterator>(nullptr), NodeOneChildVarPredicatesPtr(nullptr));

    auto value_startD = D_HOP_LOG * pre_pos;
    auto hop1min_value = hop_index->min_values_[value_startD];
    auto hop1max_value = hop_index->max_values_[value_startD];

    uncertain_level = distinct_level = 0;
    for (unsigned int range_i = 1; range_i < D_HOP_LOG; range_i++) {
      auto range_state = hop_index->mask_[value_startD + range_i];
      if (range_state == RangeState::NoExist)
        distinct_level = 1 << range_i; 
      else if (range_state == RangeState::UnExact && uncertain_level == 0)
        uncertain_level = 1 << range_i; 
    }
    
    
    if (hop_index->mask_[value_startD] == RangeState::Exact) {
      auto ow = make_shared<DfsOWIterator>();
      ow->estimate_[0] = coefficient >= 0 ? hop1min_value : hop1max_value;
      ow->estimate_[0] *= coefficient;
      ow->SetExhausted(true);
      for (unsigned int range_i = 1; range_i < D_HOP_LOG; range_i++) {
        auto range_state = hop_index->mask_[value_startD + range_i];
        if (range_state == RangeState::Exact) {
          ow->estimate_[range_i] = coefficient >= 0 ?
                                   hop_index->min_values_[value_startD + range_i] :
                                   hop_index->max_values_[value_startD + range_i];
          ow->estimate_[range_i] *= coefficient;
        } else
          ow->estimate_[range_i] = ow->estimate_[range_i - 1];
      }
      auto correspond_node = hop_index->corresponding_nodes[pre_pos];
      element e{};
      e.index = 0;
      e.cost = ow->estimate_[0];
      e.node = correspond_node;
      ow->pool_.push_back(e);

      auto ow_predicates = make_shared<NodeOneChildVarPredicates>();
      auto pre_ids = make_shared<OnePointPredicateVec>();
      if (!tree_edges.predicate_constant_[0])
        pre_ids->push_back(tree_edges.predicate_ids_[0]);
      (*ow_predicates)[correspond_node] = pre_ids;
#ifdef ITERATOR_COUNT
      OW_NUM++;
#endif
      return make_pair(ow, std::move(ow_predicates));
    }
    
  }

  auto child_candidates = TopKUtil::DfsExtendTreeEdge(parent_id, child_var, tree_edges, dfs_help_info->env);

  if (child_candidates == nullptr || child_candidates->Empty())
    return make_pair(std::shared_ptr<DfsOWIterator>(nullptr), NodeOneChildVarPredicatesPtr(nullptr));

  std::set<TYPE_ENTITY_LITERAL_ID> children_ids_set;
  std::for_each(child_candidates->cBegin(), child_candidates->cEnd(),
                [&children_ids_set](const std::pair<const TYPE_ENTITY_LITERAL_ID, 
                                                    std::shared_ptr<std::vector<TYPE_ENTITY_LITERAL_ID>>> 
                                    &p) {
                  children_ids_set.insert(p.first);
                });

  
  std::shared_ptr<std::map<TYPE_ENTITY_LITERAL_ID, double>> node_score = nullptr;
  if (has_coefficient)
    node_score = GetChildNodeScores(coefficient, children_ids_set, true, nullptr, nullptr, nullptr,
                                    dfs_help_info->env, child_var);

  

#ifdef SHOW_SCORE
  cout<<"var["<<child_var<<"] has no child, constructing OW iterators"<<endl;
#endif
  std::vector<double> children_scores;

#ifdef SHOW_SCORE
  cout<<"parent var["<<parent_id<<"] has "<<parent_child[parent_id].size()<<" child, its OW "<<endl;
#endif
  std::vector<TYPE_ENTITY_LITERAL_ID> children_ids;
  auto ow = make_shared<DfsOWIterator>();
  auto child_it = child_candidates->Begin();
  while (child_it != child_candidates->End()) {
    const auto child_id = child_it->first;
    if (children_ids_set.count(child_id)) {
#ifdef SHOW_SCORE
      cout<<"["<<child_id<<"]"<<" "<<(*node_score)[child_id];
#endif
      children_ids.push_back(child_id);
      if (has_coefficient)
        children_scores.push_back((*node_score)[child_id]);
      child_it++;
    } else
      child_it = child_candidates->Erase(child_it);
  }
#ifdef SHOW_SCORE
  cout<<endl;
      
#endif
  if (has_coefficient)
    ow->Insert(dfs_help_info->k, children_ids, children_scores);
  else
    ow->Insert(dfs_help_info->k, children_ids);
  ow->SetExhausted(true);
  
  
  double ow_best_estimate = ow->pool_.empty() ? 0.0 : ow->pool_.front().cost;
  for (unsigned int range_i = 0; range_i < D_HOP_LOG; ++range_i)
    ow->estimate_[range_i] = ow_best_estimate;
  if (pre_constant && valid_pre_to_num) {
    auto pre_pos = hop_index->GetOffset(parent_id, pre_id);

    if (pre_pos == static_cast<TYPE_ENTITY_LITERAL_ID>(-1))
      return make_pair(std::shared_ptr<DfsOWIterator>(nullptr), NodeOneChildVarPredicatesPtr(nullptr));
    auto value_startD = D_HOP_LOG * pre_pos;
    
    for (unsigned int range_i = 1; range_i < D_HOP_LOG; range_i++) {
      auto range_state = hop_index->mask_[value_startD + range_i];
      if (range_state == RangeState::Exact) {
        ow->estimate_[range_i] = coefficient >= 0 ?
                                 hop_index->min_values_[value_startD + range_i] :
                                 hop_index->max_values_[value_startD + range_i];
        ow->estimate_[range_i] *= coefficient;
      } else
        ow->estimate_[range_i] = ow->estimate_[range_i - 1];
    }
  }
#ifdef ITERATOR_COUNT
  OW_NUM++;
#endif
#ifdef ITERATOR_COUNT
  search_node_count += 1;
#endif
  return make_pair(ow, std::move(child_candidates->contents_));
}














std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFRIterator>>
DfsUtilCompressedVector::GenerateFRs(int child_var, std::shared_ptr<TopKPlanUtil::TreeEdge> &tree_edges_,
                                     std::set<TYPE_ENTITY_LITERAL_ID> &parent_var_candidates,
                                     std::set<TYPE_ENTITY_LITERAL_ID> &deleted_parents,
                                     TopKTreeNode *child_tree_node,
                                     DfsHelpInfo *dfs_help_info) {
  bool embedding_score_enabled =
      dfs_help_info->env != nullptr && dfs_help_info->env->embedding_score_context != nullptr &&
      dfs_help_info->env->embedding_score_context->Enabled();
  auto coefficient_it = dfs_help_info->coefficients->find(dfs_help_info->bgp_query->get_var_name_by_id(child_var));
  bool has_coefficient = coefficient_it != dfs_help_info->coefficients->end();
  double coefficient = has_coefficient ? (*coefficient_it).second : 0.0;
  
  
  
  std::map<TYPE_ENTITY_LITERAL_ID, shared_ptr<IDListWithAppending> > parent_child;
  std::map<TYPE_ENTITY_LITERAL_ID, std::set<TYPE_ENTITY_LITERAL_ID> > child_parent;

  std::set<TYPE_ENTITY_LITERAL_ID> child_candidates = TopKUtil::ExtendTreeEdge(parent_var_candidates,
                                                                               child_var,
                                                                               deleted_parents,
                                                                               parent_child,
                                                                               child_parent,
                                                                               tree_edges_,
                                                                               dfs_help_info->env);

  
  shared_ptr<std::map<TYPE_ENTITY_LITERAL_ID, double>> node_score = nullptr;
  if(embedding_score_enabled && has_coefficient)
    node_score = GetChildNodeScores(coefficient,child_candidates,true,&deleted_parents,&parent_child,
                                    &child_parent,dfs_help_info->env, child_var);
#ifdef FQ_COEFFICIENT
  else if(has_coefficient)
    node_score = GetChildNodeScores(coefficient,child_candidates,true,&deleted_parents,&parent_child,
                                    &child_parent,dfs_help_info->env, child_var);
#endif 
  for (auto deleted_parent : deleted_parents)
    parent_var_candidates.erase(deleted_parent);

  
  std::set<TYPE_ENTITY_LITERAL_ID> deleted_children;

  std::vector<std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFRIterator>>> descendents_FRs;
  descendents_FRs.reserve(child_tree_node->descendents_fr_num_);

  std::vector<
      std::map<TYPE_ENTITY_LITERAL_ID, 
               std::pair<std::shared_ptr<DfsOWIterator>, 
                         NodeOneChildVarPredicatesPtr>>> 
  descendents_OWs;
  descendents_OWs.reserve(child_tree_node->descendents_ow_num_);

  unsigned descendents_num = child_tree_node->descendents_.size();
  size_t function_distinct_level = 0;
  
  auto the_only_fq = static_cast<TYPE_ENTITY_LITERAL_ID>(-1);
  for (unsigned descendent_i = 0; descendent_i < descendents_num; descendent_i++) {
    auto descendent_plan = child_tree_node->descendents_[descendent_i];
    auto &descendent_tree_edges_plan = child_tree_node->tree_edges_[descendent_i];
    auto is_fr = descendent_plan->descendents_.size() != 0;

    if (is_fr) {
      
      if (dfs_help_info->max_leaf_distance_[child_var] <= 3) {
        std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFRIterator>> descendent_iterators;
        std::set<TYPE_ENTITY_LITERAL_ID> deleted_child_this_iterator;
        for (auto fq_id : child_candidates) {
          size_t fq_distinct_level = 0;
          size_t fq_uncertain_level = 0;
          auto fr = ExploreFRs(child_var,
                               fq_id,
                               descendent_plan->var_id,
                               fq_distinct_level,
                               fq_uncertain_level,
                               *descendent_tree_edges_plan,
                               dfs_help_info);
          
          if (fr == nullptr || fr->Empty()) {
            deleted_child_this_iterator.insert(fq_id);
            continue;
          }
          descendent_iterators[fq_id] = std::move(fr);
          
          if (fq_distinct_level > 0) {
            function_distinct_level = max(function_distinct_level, fq_distinct_level);
            the_only_fq = fq_id;
            break;
          }
        }
        for (auto deleted_child_id : deleted_child_this_iterator) {
          dfs_help_info->node_caches->var_invalid_ids[child_var].insert(deleted_child_id);
          child_candidates.erase(deleted_child_id);
          for (auto parent_id : child_parent[deleted_child_id]) {
            parent_child[parent_id]->Erase(deleted_child_id);
            if (parent_child[parent_id]->MainKeyNum() == 0) {
              deleted_parents.insert(parent_id);
              parent_var_candidates.erase(parent_id);
            }
          }
        }
        descendents_FRs.push_back(std::move(descendent_iterators));
        if (function_distinct_level > 0) {
          
          for (auto deleted_child_id : child_candidates) {
            if (deleted_child_id == the_only_fq)
              continue;
            for (auto parent_id : child_parent[deleted_child_id]) {
              parent_child[parent_id]->Erase(deleted_child_id);
              if (parent_child[parent_id]->MainKeyNum() == 0) {
                deleted_parents.insert(parent_id);
                parent_var_candidates.erase(parent_id);
              }
            }
          }
          child_candidates.clear();
          child_candidates.insert(the_only_fq);
        }
      } else {
        auto descendent_iterators = GenerateFRs(descendent_plan->var_id,
                                                descendent_tree_edges_plan,
                                                child_candidates,
                                                deleted_children,
                                                descendent_plan,
                                                dfs_help_info);
        descendents_FRs.push_back(std::move(descendent_iterators));
      }
    } else {
      std::map<TYPE_ENTITY_LITERAL_ID, 
               std::pair<std::shared_ptr<DfsOWIterator>, 
                         NodeOneChildVarPredicatesPtr>> 
      descendent_iterators;

      std::set<TYPE_ENTITY_LITERAL_ID> deleted_child_this_iterator;
      for (auto fq_id : child_candidates) {
        size_t ow_distinct_level = 0;
        
        size_t ow_uncertain_level = 0;
        auto ow_pair = ExploreOW(fq_id,
                                 descendent_plan->var_id,
                                 ow_distinct_level,
                                 ow_uncertain_level,
                                 *descendent_tree_edges_plan,
                                 dfs_help_info);
        
        if (ow_pair.first == nullptr || ow_pair.first->Empty()) {
          
          deleted_child_this_iterator.insert(fq_id);
          continue;
        }
        descendent_iterators[fq_id] = std::move(ow_pair);
        if (ow_distinct_level > 0) {
          function_distinct_level = max(function_distinct_level, ow_distinct_level);
          the_only_fq = fq_id;
          break;
        }
      }

      for (auto deleted_child_id : deleted_child_this_iterator) {
        child_candidates.erase(deleted_child_id);
        for (auto parent_id : child_parent[deleted_child_id]) {
          parent_child[parent_id]->Erase(deleted_child_id);
          if (parent_child[parent_id]->MainKeyNum() == 0) {
            deleted_parents.insert(parent_id);
            parent_var_candidates.erase(parent_id);
          }
        }
      }
      descendents_OWs.push_back(std::move(descendent_iterators));
      if (function_distinct_level > 0) {
        
        for (auto deleted_child_id : child_candidates) {
          if (deleted_child_id == the_only_fq)
            continue;
          for (auto parent_id : child_parent[deleted_child_id]) {
            parent_child[parent_id]->Erase(deleted_child_id);
            if (parent_child[parent_id]->MainKeyNum() == 0) {
              deleted_parents.insert(parent_id);
              parent_var_candidates.erase(parent_id);
            }
          }
        }
        child_candidates.clear();
        child_candidates.insert(the_only_fq);
      }
    }
  }

  
  auto child_fqs = AssemblingFrOw(child_candidates,
                                  child_var, dfs_help_info->k,
                                  node_score, descendents_FRs, descendents_OWs);

  
  for (auto deleted_child_id : deleted_children) {
    for (auto parent_id : child_parent[deleted_child_id]) {
      auto &children = parent_child[parent_id];
      children->Erase(deleted_child_id);
      if (children->MainKeyNum() == 0) {
        deleted_parents.insert(parent_id);
        parent_var_candidates.erase(parent_id);
      }
    }
  }

  std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFRIterator>> result;

  
  
  for (auto parent_id : parent_var_candidates) {
    auto fr = make_shared<DfsFRIterator>();
    for (auto &child_id_appending_pair : *(parent_child[parent_id])->contents_) {
      auto child_id = child_id_appending_pair.first;
      auto &appending_list = child_id_appending_pair.second;
      fr->Insert(child_id, child_fqs[child_id], appending_list);
    }
    result[parent_id] = fr;
    fr->SetExhausted(false);
    fr->SetNotFullyExplored(false);
    fr->Estimate();
#ifdef ITERATOR_COUNT
    FR_NUM++;
#endif
  }

  return result;

}










shared_ptr<DfsFQIterator> DfsUtilCompressedVector::ExploreFQ(TYPE_ENTITY_LITERAL_ID chosen_child,
                                                             int child_var,
                                                             size_t &distinct_level,
                                                             size_t &uncertain_level,
                                                             DfsHelpInfo *dfs_help_info) {
  shared_ptr<DfsFQIterator> fq(nullptr);
  auto node_caches = dfs_help_info->node_caches;
  if (node_caches->var_invalid_ids[child_var].count(chosen_child))
    return fq;

  auto cache_it = node_caches->var_valid_ids[child_var].find(chosen_child);
  if (cache_it != node_caches->var_valid_ids[child_var].end()) {
#ifdef ITERATOR_COUNT
    
#endif
    return std::dynamic_pointer_cast<DfsFQIterator>(cache_it->second);
  }
  auto child_tree_node = dfs_help_info->node_plan[child_var];
  unsigned descendents_num = child_tree_node->descendents_.size();

  std::vector<std::shared_ptr<DfsFRIterator>> descendents_FRs;
  std::vector<std::pair<std::shared_ptr<DfsOWIterator>, 
                        NodeOneChildVarPredicatesPtr>> 
  descendents_OWs;
  for (unsigned descendent_i = 0; descendent_i < descendents_num; descendent_i++) {
    auto descendent_plan = child_tree_node->descendents_[descendent_i];
    auto &descendent_tree_edges_plan = child_tree_node->tree_edges_[descendent_i];
    auto is_fr = descendent_plan->descendents_.size() != 0;
    if (is_fr) {
      remove_reference<decltype(distinct_level)>::type fr_distinct_level = 0;
      remove_reference<decltype(distinct_level)>::type fr_uncertain_level = 0;
      auto fr = ExploreFRs(child_var,
                           chosen_child,
                           descendent_plan->var_id,
                           fr_distinct_level,
                           fr_uncertain_level,
                           *descendent_tree_edges_plan,
                           dfs_help_info);
      
      if (fr == nullptr || fr->Empty()) {
        node_caches->var_invalid_ids[child_var].insert(chosen_child);
        
        return fq;
      }
      
      if (fr_uncertain_level == 1) {

      }
      descendents_FRs.push_back(std::move(fr));
      distinct_level |= fr_distinct_level;
      uncertain_level |= fr_uncertain_level;
    } else {
      remove_reference<decltype(distinct_level)>::type ow_distinct_level = 0;
      remove_reference<decltype(distinct_level)>::type ow_uncertain_level = 0;
      auto ow_pair = ExploreOW(chosen_child,
                               descendent_plan->var_id,
                               ow_distinct_level,
                               ow_uncertain_level,
                               *descendent_tree_edges_plan,
                               dfs_help_info);
      
      if (ow_pair.first == nullptr || ow_pair.first->Empty()) {
        node_caches->var_invalid_ids[child_var].insert(chosen_child);
        
        return fq;
      }
      descendents_OWs.push_back(std::move(ow_pair));
      distinct_level |= ow_distinct_level;
      uncertain_level |= ow_uncertain_level;
    }
  }
  distinct_level = distinct_level > 0 ? distinct_level >> 1 : 0;
  uncertain_level = uncertain_level > 0 ? uncertain_level >> 1 : 0;

  double node_score = 0.0;
  bool embedding_score_enabled =
      dfs_help_info->env != nullptr && dfs_help_info->env->embedding_score_context != nullptr &&
      dfs_help_info->env->embedding_score_context->Enabled();
  auto coefficient_it = dfs_help_info->coefficients->find(dfs_help_info->bgp_query->get_var_name_by_id(child_var));
  bool has_coefficient = coefficient_it != dfs_help_info->coefficients->end();
  if (embedding_score_enabled && has_coefficient) {
    double coefficient = (*coefficient_it).second;
    std::set<TYPE_ENTITY_LITERAL_ID> chosen_ids;
    chosen_ids.insert(chosen_child);
    auto node_scores = GetChildNodeScores(coefficient, chosen_ids, true, nullptr, nullptr,
                                          nullptr, dfs_help_info->env, child_var);
    auto score_it = node_scores->find(chosen_child);
    if (score_it == node_scores->end()) {
      node_caches->var_invalid_ids[child_var].insert(chosen_child);
      return fq;
    }
    node_score = score_it->second;
  }
#ifdef FQ_COEFFICIENT
  else if (has_coefficient) {
    double coefficient = (*coefficient_it).second;
    std::set<TYPE_ENTITY_LITERAL_ID> chosen_ids;
    chosen_ids.insert(chosen_child);
    auto node_scores = GetChildNodeScores(coefficient, chosen_ids, true, nullptr, nullptr,
                                          nullptr, dfs_help_info->env, child_var);
    auto score_it = node_scores->find(chosen_child);
    if (score_it == node_scores->end()) {
      node_caches->var_invalid_ids[child_var].insert(chosen_child);
      return fq;
    }
    node_score = score_it->second;
  }
#endif

  
  fq = AssemblingTheFq(chosen_child, node_score, child_var, dfs_help_info->k, descendents_FRs, descendents_OWs);
  node_caches->var_valid_ids[child_var][chosen_child] = fq;
  return fq;
}






std::shared_ptr<DfsFRIterator>
DfsUtilCompressedVector::ExploreFRs(int parent_var,
                                    int parent_id,
                                    int child_var,
                                    size_t &distinct_level,
                                    size_t &uncertain_level,
                                    const TopKPlanUtil::TreeEdge &tree_edges_,
                                    DfsHelpInfo *dfs_help_info) {
  auto r = std::shared_ptr<DfsFRIterator>(nullptr);
#ifdef FQ_COEFFICIENT
  auto coefficient_it = dfs_help_info->coefficients->find(dfs_help_info->bgp_query->get_var_name_by_id(child_var));
  bool has_coefficient = coefficient_it != dfs_help_info->coefficients->end();
  double coefficient = has_coefficient ? (*coefficient_it).second : 0.0;
#endif
  
  
  
  auto children_predicates = TopKUtil::DfsExtendTreeEdge(parent_id, child_var,
                                                         tree_edges_, dfs_help_info->env);
  auto node_caches = dfs_help_info->node_caches;
  if (children_predicates == nullptr || children_predicates->Empty()) {
    node_caches->var_invalid_ids[parent_var].insert(parent_id);
    return r;
  }





  distinct_level = 0;
  uncertain_level = -1;

  TYPE_ENTITY_LITERAL_ID chosen_child;
  shared_ptr<DfsFQIterator> fq(nullptr);
  bool success_found = false;
  auto fq_it = children_predicates->Begin();
  
  
  
  
  while (fq_it != children_predicates->End()) {
    size_t child_distinct_level = 0;
    size_t child_uncertain_level = 0;
    chosen_child = fq_it->first;
    fq = ExploreFQ(chosen_child, child_var, child_distinct_level, child_uncertain_level, dfs_help_info);





    if (fq == nullptr)
      fq_it = children_predicates->Erase(fq_it);
    else {
      
      if (r == nullptr)
        r = make_shared<DfsFRIterator>();
      r->Insert(chosen_child, fq, fq_it->second);
      fq_it = children_predicates->Erase(fq_it);

      
      for (int estimate_pos = 0; estimate_pos < D_HOP_LOG; estimate_pos++) {
        auto mask = (0x1) << estimate_pos;
        
        if ((child_uncertain_level & mask) == 0) {
          
          if (uncertain_level & mask)
            r->estimate_[estimate_pos] = fq->estimate_[estimate_pos];
          else
            r->estimate_[estimate_pos] = std::min(r->estimate_[estimate_pos], fq->estimate_[estimate_pos]);
          r->has_estimated_ = true;
        } else
          break;
      }
      distinct_level = child_distinct_level | distinct_level;
      uncertain_level = child_uncertain_level & uncertain_level;
      
      
      if (((uncertain_level & 0x1) == 0) || distinct_level > 0) {
        break;
      }
    }
  }
  if (r == nullptr)
    return r;

  if (distinct_level > 0) {
    const auto end_it = children_predicates->End();
    for (auto it = children_predicates->Begin(); it != end_it; it++) {
      if (it->first != chosen_child)
        node_caches->var_invalid_ids[child_var].insert(it->first);
    }
    children_predicates->OnlyRemain(chosen_child);
  }

  
  r->SetUnexploredFqInfo(children_predicates);
  r->Estimate();
#ifdef ITERATOR_COUNT
  FR_NUM++;
#endif



  return r;
}
