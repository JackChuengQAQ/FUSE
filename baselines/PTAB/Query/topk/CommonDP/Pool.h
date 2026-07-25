
#ifndef TOPK_DPB_POOL_H_
#define TOPK_DPB_POOL_H_
#include "../../../Util/Util.h"

class DPBList;

using OnePointPredicateVec= std::vector<TYPE_ENTITY_LITERAL_ID>;
using OnePointPredicatePtr = std::shared_ptr<OnePointPredicateVec>;
using NodeOneChildVarPredicates = std::map<TYPE_ENTITY_LITERAL_ID ,OnePointPredicatePtr>;
using NodeOneChildVarPredicatesPtr = std::shared_ptr<NodeOneChildVarPredicates>;

struct element{
  TYPE_ENTITY_LITERAL_ID node;
  unsigned int index;
  double cost;
  bool operator<(const element &other) const {return this->cost < other.cost;}
  bool operator!=(const element &other) const {
    return this->node!=other.node || this->cost!=other.cost || this->index !=other.index;
  }
};

struct kTPMElement{
  TYPE_ENTITY_LITERAL_ID node;
  double cost;
  bool operator<(const element &other) const {return this->cost < other.cost;}
  bool operator!=(const element &other) const {
    return this->node!=other.node || this->cost!=other.cost;
  }
};

struct SAEElement{
  TYPE_ENTITY_LITERAL_ID node;
  double cost;
  bool operator<(const element &other) const {return this->cost < other.cost;}
  bool operator!=(const element &other) const {
    return this->node!=other.node || this->cost!=other.cost;
  }
};

using EagerElement = kTPMElement;
using TakeAllElement = kTPMElement;

struct Take2Element{
  TYPE_ENTITY_LITERAL_ID node;
  double cost;
  Take2Element(TYPE_ENTITY_LITERAL_ID node_id, double subtree_cost):node(node_id),cost(subtree_cost){}
  bool operator<(const Take2Element &other) const {return this->cost > other.cost;}
  bool operator!=(const element &other) const {
    return this->node!=other.node || this->cost!=other.cost;
  }
};


using  Pool = std::vector<element>;





using  sequence =  std::vector<unsigned int>;


struct FqElement{
 public:
  sequence seq;
  double cost;
  FqElement() = default;
  FqElement(sequence s, double c):seq(std::move(s)),cost(c) {};
  bool operator<(const FqElement& other) const {return this->cost < other.cost;}
  bool operator!=(const FqElement& other) const {
    return this->cost != other.cost || !std::equal(this->seq.begin(),this->seq.end(),other.seq.begin());
  }
};

using  ePool = std::vector<FqElement>;

#endif 
