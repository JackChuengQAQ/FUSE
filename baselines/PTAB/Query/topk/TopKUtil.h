
#ifndef GSTOREGDB_QUERY_TOPK_DPB_TOPKUTIL_H_
#define GSTOREGDB_QUERY_TOPK_DPB_TOPKUTIL_H_

#include "../../Util/Util.h"
#include "../../KVstore/KVstore.h"
#include "../../Query/SPARQLquery.h"
#include "../../Query/BasicQuery.h"
#include "../../Database/Statistics.h"
#include "../../Query/QueryTree.h"
#include "../../Query/IDList.h"
#include "../../Database/TableOperator.h"
#include "TopKSearchPlan.h"
#include "../../Util/EmbeddingGraph.h"

namespace TopKUtil {


struct Env{
  KVstore *kv_store;
  BasicQuery *basic_query;
  shared_ptr<BGPQuery> bgp_query;
  shared_ptr<unordered_map<TYPE_ENTITY_LITERAL_ID,shared_ptr<IDList>>> id_caches;
  int k;
  
  std::shared_ptr<std::vector<std::map<TYPE_ENTITY_LITERAL_ID,std::set<TYPE_ENTITY_LITERAL_ID> >>> non_tree_edges_lists_;
  std::shared_ptr<unordered_map<std::string, double>> coefficients;
  shared_ptr<Transaction> txn;
  std::shared_ptr<stringstream> ss;
  std::vector<std::vector<bool>> ow_coefficient_counts_;
  std::vector<bool> subtree_has_coefficient_;
  std::vector<size_t> max_leave_distance_;
  std::shared_ptr<EmbeddingGraph::ScoreContext> embedding_score_context;
};

void SetEmbeddingScoreContext(std::shared_ptr<EmbeddingGraph::ScoreContext> context);
std::shared_ptr<EmbeddingGraph::ScoreContext> GetEmbeddingScoreContext();
void ClearGroundTruthOutputLines();
void AddGroundTruthOutputLine(const std::string &line);
bool HasGroundTruthOutputLines();
bool WriteGroundTruthOutput(FILE *fp);

void UpdateIDList(shared_ptr<IDList> valid_id_list, unsigned* id_list, unsigned id_list_len,bool id_list_prepared);

void UpdateIDListWithAppending(shared_ptr<IDListWithAppending> &ids_appending, unsigned* id_list,
                               unsigned id_list_len,unsigned  one_record_len,
                               bool id_list_prepared,unsigned main_key_position);

std::set<TYPE_ENTITY_LITERAL_ID> 
ExtendTreeEdge(std::set<TYPE_ENTITY_LITERAL_ID>& parent_var_candidates,int child_var,
               std::set<TYPE_ENTITY_LITERAL_ID>& deleted_parents,
               std::map<TYPE_ENTITY_LITERAL_ID, shared_ptr<IDListWithAppending>  > &parent_child,
               std::map<TYPE_ENTITY_LITERAL_ID, std::set<TYPE_ENTITY_LITERAL_ID> > &child_parent,
               const std::shared_ptr<TopKPlanUtil::TreeEdge>& tree_edges_,
               Env *env);

shared_ptr<IDListWithAppending>
DfsExtendTreeEdge(TYPE_ENTITY_LITERAL_ID parent_id,int child_var,
                  const TopKPlanUtil::TreeEdge& tree_edges_,
                  Env *env);

void AddRelation(TYPE_ENTITY_LITERAL_ID x,TYPE_ENTITY_LITERAL_ID y, std::map<TYPE_ENTITY_LITERAL_ID ,std::set<TYPE_ENTITY_LITERAL_ID >>& mapping);


void CalculatePosVarMappingNode(TopKTreeNode* top_k_tree_node,shared_ptr<std::map<TYPE_ENTITY_LITERAL_ID, TYPE_ENTITY_LITERAL_ID>> pos_var_mapping);
shared_ptr<std::map<TYPE_ENTITY_LITERAL_ID, TYPE_ENTITY_LITERAL_ID>> CalculatePosVarMapping(shared_ptr<TopKSearchPlan> search_plan);

void FillArrayOfTreeNode(TopKTreeNode* top_k_tree_node,TopKTreeNode** node_plan);

double GetScore(string &v, stringstream &ss);
void GetVarCoefficientsTreeNode(QueryTree::CompTreeNode *comp_tree_node,
                                std::unordered_map<std::string,double>& coefficients,
                                bool minus_signed=false);


std::shared_ptr<std::unordered_map<std::string,double>> getVarCoefficients(QueryTree::Order order);
std::shared_ptr<std::unordered_map<std::string,double>>
getEmbeddingSumCoefficients(const shared_ptr<BGPQuery> &bgp_query);

std::shared_ptr< std::map<TYPE_ENTITY_LITERAL_ID,double>>
GetChildNodeScores(double coefficient,
                   std::set<TYPE_ENTITY_LITERAL_ID> &child_candidates,
                   bool has_parent = false,
                   std::set<TYPE_ENTITY_LITERAL_ID>* deleted_parents = nullptr,
                   std::map<TYPE_ENTITY_LITERAL_ID, shared_ptr<IDListWithAppending>  > *parent_child = nullptr,
                   std::map<TYPE_ENTITY_LITERAL_ID, std::set<TYPE_ENTITY_LITERAL_ID> > *child_parent = nullptr,
                   Env *env= nullptr,
                   int query_var_id = -1);

};

#endif 
