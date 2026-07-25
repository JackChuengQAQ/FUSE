
#ifndef GSTORELIMITK_QUERY_TOPK_DFS_TOPKUTIL_H_
#define GSTORELIMITK_QUERY_TOPK_DFS_TOPKUTIL_H_


#include "../TopKUtil.h"
#include "../CommonDP/Pool.h"
#include "./DfsFQIterator.h"
#include "./DfsFRIterator.h"
#include "./DfsOWIterator.h"
#include "./HopIndex.h"

namespace DfsUtilCompressedVector{


std::map<TYPE_ENTITY_LITERAL_ID,std::shared_ptr<DfsFQIterator>>  AssemblingFrOw(std::set<TYPE_ENTITY_LITERAL_ID> &fq_ids,
                                                                                int fq_var_id, int k,
                                                                                std::shared_ptr<std::map<TYPE_ENTITY_LITERAL_ID,double>> &node_scores,
                                                                                std::vector<std::map<TYPE_ENTITY_LITERAL_ID, std::shared_ptr<DfsFRIterator>>> &descendents_FRs,
                                                                                std::vector<std::map<TYPE_ENTITY_LITERAL_ID,std::pair<std::shared_ptr<DfsOWIterator>, NodeOneChildVarPredicatesPtr>>>&descendents_OWs);
std::shared_ptr<DfsFQIterator>  AssemblingTheFq(TYPE_ENTITY_LITERAL_ID fq_id,
                                                double node_score, int fq_var_id, int k,
                                                std::vector<std::shared_ptr<DfsFRIterator>> &descendents_FRs,
                                                std::vector<std::pair<std::shared_ptr<DfsOWIterator>, NodeOneChildVarPredicatesPtr>> &descendents_OWs
                                                );


std::pair<std::shared_ptr<DfsOWIterator>, 
          NodeOneChildVarPredicatesPtr> 
ExploreOW(TYPE_ENTITY_LITERAL_ID parent_id,
          int child_var,
          size_t &distinct_level,
          size_t &uncertain_level,
          const TopKPlanUtil::TreeEdge& tree_edges,
          DfsHelpInfo *dfs_help_info);

std::map<TYPE_ENTITY_LITERAL_ID,std::shared_ptr<DfsFRIterator>>  GenerateFRs(int child_var, std::shared_ptr<TopKPlanUtil::TreeEdge>& tree_edges_,
                                                                             std::set<TYPE_ENTITY_LITERAL_ID>& parent_var_candidates,
                                                                             std::set<TYPE_ENTITY_LITERAL_ID>& deleted_parents,
                                                                             TopKTreeNode *child_tree_node,
                                                                             DfsHelpInfo *dfs_help_info);

shared_ptr<DfsFQIterator> ExploreFQ(TYPE_ENTITY_LITERAL_ID chosen_child,
                                    int child_var,
                                    size_t &distinct_level,
                                    size_t &uncertain_level,
                                    DfsHelpInfo *dfs_help_info);

std::shared_ptr<DfsFRIterator>
ExploreFRs(int parent_var,
           int parent_id,
           int child_var,
           size_t &distinct_level,
           size_t &uncertain_level,
           const TopKPlanUtil::TreeEdge &tree_edges_,
           DfsHelpInfo *dfs_help_info);


DfsFRIterator* BuildIteratorTree(const shared_ptr<TopKSearchPlan> tree_search_plan,  DfsHelpInfo *dfs_help_info);


}
#endif 
