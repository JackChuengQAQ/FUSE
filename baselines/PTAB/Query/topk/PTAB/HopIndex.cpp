
#include "HopIndex.h"

int avg_degree = 10;
char RangeStateToChar(RangeState state){
  char r = 0;
  switch (state) {
    case  RangeState::Exact:
      r = 1;
      break;
    case RangeState::UnExact:
      r = 2;
      break;
    case RangeState::NoExist:
      r= 3;
      break;
    default:
      break;
  }
  return r;
}

RangeState CharToRangeState(char x)
{
  RangeState r = RangeState::NoExist;
  switch (x) {
    case 1:
      r =  RangeState::Exact;
      break;
    case 2:
      r =  RangeState::UnExact;
      break;
    case 3:
      r =  RangeState::NoExist;
      break;
    default:
      break;
  }
  return r;
}

std::unique_ptr<std::map<TYPE_PREDICATE_ID,TopTwoInfo>> HopIndex::MergeSameTypeNodes(const std::set<TYPE_ENTITY_LITERAL_ID>& ids) const
{
  
  auto r_ptr = std::unique_ptr<std::map<TYPE_PREDICATE_ID,TopTwoInfo>>(new std::map<TYPE_PREDICATE_ID,TopTwoInfo>() ) ;
  auto &r = *r_ptr;
  for(auto id : ids)
  {
    auto id_offset = this->entity_offset[id];
    auto id_offsetD = id_offset * D_HOP_LOG;
    auto id_pre_num = this->predicates_num[id];
    for(unsigned int p =0; p<id_pre_num;p++)
    {
      auto pre_id = this->predicate_ids[id_offset+p];
      auto min_v = this->min_values_[id_offsetD+p*D_HOP_LOG];
      auto max_v = this->max_values_[id_offsetD+p*D_HOP_LOG];
      if(r.find(pre_id)==r.end())
        r[pre_id] = TopTwoInfo(id,min_v,max_v);
      else {
        r[pre_id].AddMinNode(id, min_v);
        r[pre_id].AddMaxNode(id, max_v);
      }
    }
  }
  return r_ptr;
}

size_t HopIndex::CountAttrNum(const std::set<TYPE_ENTITY_LITERAL_ID>& ids) const
{
  set<TYPE_PREDICATE_ID> unique_pre;
  for(auto id : ids)
  {
    auto off_start = this->entity_offset[id];
    auto pre_num = this->predicates_num[id];
    for(unsigned int p = 0;p<pre_num;p++)
    {
      auto pre_id = this->predicate_ids[off_start+p];
      unique_pre.insert(pre_id);
    }
  }
  return unique_pre.size();
}

void HopIndex::LoadPredicates(KVstore *kv_store,std::string file_name)
{
  ifstream infile(file_name,ios::in);
  if(!infile){
    cout << "HopIndex::LoadPredicates Error in open " << file_name << endl;
    perror("ifstream");
    exit(1);
  }
  bool type_pre_loaded = false;
  while (!infile.eof())
  {
    string t;
    infile>>t;
    if(t.empty())
      continue;
    auto p_id = kv_store->getIDByPredicate(t);
    if(!type_pre_loaded) {
      this->type_pre_id_ = p_id;
      type_pre_loaded = true;
    }
    else if(p_id!=INVALID)
      this->predicates_to_number_.insert(p_id);
  }
  return;
}






void HopIndex::BuildNodeInfo(KVstore *kv_store) {
  this->entity_offset = std::unique_ptr<size_t[]>(new size_t[this->entity_num_]);
  this->predicates_num = std::unique_ptr<size_t[]>(new size_t[this->entity_num_]);
  
  long t1 = Util::get_cur_time();
  cout<<"Get in BuildNodeInfo"<<endl;
  unsigned *id_type_list = nullptr;
  unsigned element_num = 0;
  string type_pre = "type";
  
    
    
    
      cout << "type_pre_id: " << type_pre_id_ << "  " << kv_store->getPredicateByID(type_pre_id_) << endl;
      kv_store->getsubIDobjIDlistBypreID(this->type_pre_id_, id_type_list, element_num, true, nullptr);
    
    
  
  
  for (unsigned int i = 0; i < element_num; i += 2) {
    this->entities_type_[id_type_list[i]] = id_type_list[i+1];
  }
  delete[] id_type_list;

  long t2 = Util::get_cur_time();

  cout << "build type array, used " << (t2 - t1) << "ms." << endl;
  
  cout << "Now build each node self-information."<<endl;

  
  vector<TYPE_PREDICATE_ID> pre_till_now;
  size_t offset_now = 0;
  for (unsigned int i = 0; i < this->entity_num_; i++) {
    if (this->entities_type_[i] == TypeInvalid) {
      this->entity_offset[i] = -1;
      this->predicates_num[i] = 0;
      continue;
    }
    this->entity_offset[i] = offset_now;
    unsigned *pre_number_list = nullptr;
    unsigned list_item_num = 0;
    kv_store->getpreIDobjIDlistBysubID(i, pre_number_list, list_item_num);
    size_t pre_num = 0;
    const auto pair_num = list_item_num / 2;
    for (unsigned int j = 0; j < pair_num; j++) {
      auto pre_id = static_cast<TYPE_PREDICATE_ID>(pre_number_list[2 * j]);
      if (this->predicates_to_number_.find(pre_id) == this->predicates_to_number_.end())
        continue;
      if (offset_now == pre_till_now.size() || pre_till_now.back() != pre_id)
        pre_till_now.push_back(pre_id);
    }
    offset_now = pre_till_now.size();
    delete[]pre_number_list;
  }
  this->predicate_total = pre_till_now.size();
  this->predicate_ids =  std::unique_ptr<TYPE_PREDICATE_ID[]>(new TYPE_PREDICATE_ID[predicate_total]);
  this->corresponding_nodes = std::unique_ptr<TYPE_ENTITY_LITERAL_ID []>(new TYPE_ENTITY_LITERAL_ID[predicate_total]);
  this->min_values_ = std::unique_ptr<double[]>(new double[predicate_total*D_HOP_LOG]);
  this->max_values_ = std::unique_ptr<double[]>(new double[predicate_total*D_HOP_LOG]);
  this->mask_ = std::unique_ptr<RangeState[]>(new RangeState[predicate_total*D_HOP_LOG]);

  vector<double> max_value;
  vector<double> min_value;
  vector<TYPE_PREDICATE_ID> predicates;
  vector<TYPE_ENTITY_LITERAL_ID > literals;
  vector<int> is_unique_value;
  valid_num = 0;
  cout<<" entity num "<<this->entity_num_<<endl;
  for (unsigned int i = 0; i < this->entity_num_; i++) {
#ifdef HOP_INDEX_DEBUG_INFO
    if ((i + 1) % 5000000 == 0) {
      cout << "BuildHopInfo node " << i + 1 << " accumulated valid num: " << valid_num << endl;
      cout << "used " << Util::get_cur_time() - t1 << "ms. " << endl;
    }
#endif
    if (this->entities_type_[i] == TypeInvalid)
      continue;
    valid_num++;
    auto node_offset = this->entity_offset[i];
    unsigned *pre_number_list = nullptr;
    unsigned list_item_num = 0;
    kv_store->getpreIDobjIDlistBysubID(i, pre_number_list, list_item_num);
    size_t pre_num = 0;
    const auto pair_num = list_item_num / 2;
    for (unsigned int j = 0; j < pair_num; j++) {
      auto pre_id = static_cast<TYPE_PREDICATE_ID>(pre_number_list[2 * j]);
      auto number_id = pre_number_list[2 * j + 1];
      bool new_pre_detected = false;
      if (this->predicates_to_number_.find(pre_id) == this->predicates_to_number_.end())
        continue;
      if (predicates.empty() || predicates.back() != pre_id) {
        new_pre_detected = true;
      }
      
      auto numerical_string = kv_store->getStringByID(number_id);
      auto check_result = Util::checkGetNumericLiteral(numerical_string);
      auto success = get<0>(check_result);
      if (success) {
        auto new_value = get<1>(check_result);
        if (new_pre_detected) {
          pre_num++;
          predicates.push_back(pre_id);
          max_value.push_back(new_value);
          min_value.push_back(new_value);
          literals.push_back(number_id);
          is_unique_value.push_back(1);
        } else {
          max_value.back() = std::max(max_value.back(), new_value);
          min_value.back() = std::min(min_value.back(), new_value);
          is_unique_value.back() = 0;
        }
      }
    }
    this->predicates_num[i] = pre_num;
    for (unsigned int p = 0; p < pre_num; p++) {
      auto pos = node_offset + p;
      auto posD = D_HOP_LOG * pos;
      auto pre_id = predicates[p];
      this->predicate_ids[pos] = pre_id;
      this->corresponding_nodes[pos] = literals[p];
      this->min_values_[posD] = min_value[p];
      this->max_values_[posD] = max_value[p];
      if (is_unique_value[p])
        this->mask_[posD] = RangeState::Exact;
      else
        this->mask_[posD] = RangeState::UnExact;
      for (unsigned int j = 1; j < D_HOP_LOG; j++)
        this->mask_[posD + j] = RangeState::NoExist;
    }
    max_value.clear();
    min_value.clear();
    predicates.clear();
    literals.clear();
    is_unique_value.clear();
    delete[]pre_number_list;
  }
  cout << " Valid Num :"<<valid_num<<endl;
  long t3 = Util::get_cur_time();
  cout << "build each node info used " << (t3 - t2) << "ms." << endl;
}

const size_t no_limit_range = static_cast<size_t>(-1);
struct HopHelpInfo{
  
  std::map<TYPE_ENTITY_LITERAL_ID,std::set<TYPE_ENTITY_LITERAL_ID>> one_hop_neighbour;
  std::map<TYPE_ENTITY_LITERAL_ID,std::set<TYPE_ENTITY_LITERAL_ID>> two_hop_neighbour;

  
  std::map<TYPE_ENTITY_LITERAL_ID,std::unique_ptr<std::map<TYPE_PREDICATE_ID,TopTwoInfo>>> merged_one_hop;

  
  
  
  std::map<TYPE_ENTITY_LITERAL_ID, std::map<TYPE_PREDICATE_ID,TopTwoInfo>> merged_info_2_hop;

  size_t farthest_range = no_limit_range;
  size_t see_all_range = no_limit_range;
};









void HopIndex::BuildHopInfo(KVstore *kv_store) {
  long t1 = Util::get_cur_time();
  cout<<"Get in BuildHopInfo"<<endl;
  
  vector<std::unique_ptr<HopHelpInfo>> infos(this->entity_num_);

  
  
  std::list<std::pair<TYPE_ENTITY_LITERAL_ID,std::vector<TYPE_ENTITY_LITERAL_ID>>> neighbour_list;

  const std::vector<TYPE_ENTITY_LITERAL_ID > vec_template;
  const pair<unsigned int, std::vector<TYPE_ENTITY_LITERAL_ID >> pair_template = make_pair((TYPE_ENTITY_LITERAL_ID)0, vec_template);
  
  cout<<"entity_num_:"<<this->entity_num_<<endl;
  std::set<TYPE_ENTITY_LITERAL_ID > unique_ids;

  
  for(TYPE_ENTITY_LITERAL_ID i = 0;i<this->entity_num_;i++) {
    if (this->entities_type_[i] == TypeInvalid)
      continue;
    infos[i].reset(new HopHelpInfo);
  }

  struct CostEstimation{
    TYPE_ENTITY_LITERAL_ID id;
    size_t cost;
    CostEstimation():id(-1),cost(0){};
    bool operator<(const CostEstimation& other)const {
      return this->cost < other.cost;
    }
  };
  std::unique_ptr<minmax::MinMaxHeap<CostEstimation>> memory_max_heap(new minmax::MinMaxHeap<CostEstimation>);
  std::set<TYPE_ENTITY_LITERAL_ID > left_behind_kick_ids;
  size_t kick_out_neighbour_nums = 0;
  size_t kick_out_time = 0;
  size_t memory_now = 0;
  size_t  terminated_added = 0;
  
  
  for (TYPE_ENTITY_LITERAL_ID i = 0; i < this->entity_num_; i++) {
    if ((i + 1) % 5000000 == 0) {
      cout << "Preprocess 1-hop neighbour nodes " << i + 1 << endl;
      cout << "used " << Util::get_cur_time() - t1 << "ms. " << endl;
    }
    if (this->entities_type_[i] == TypeInvalid)
      continue;

    auto i_type = this->entities_type_[i];
    unsigned *pre_obj_list = nullptr;
    unsigned int pre_obj_num;
    
    kv_store->getpreIDobjIDlistBysubID(i, pre_obj_list, pre_obj_num, true);

    unsigned *pre_sub_list = nullptr;
    unsigned int pre_sub_num;
    
    kv_store->getpreIDsubIDlistByobjID(i, pre_sub_list, pre_sub_num, true);

    pre_obj_num = pre_obj_num / 2;
    pre_sub_num = pre_sub_num / 2;
    for (unsigned int o_i = 0; o_i < pre_obj_num; o_i++) {
      auto pre_id = pre_obj_list[o_i * 2];
      auto obj_id = pre_obj_list[o_i * 2 + 1];
      if (pre_id == this->type_pre_id_)
        continue;
      if (obj_id < Util::LITERAL_FIRST_ID && obj_id < this->entity_num_
          && this->entities_type_[obj_id] != TypeInvalid)
        unique_ids.insert(obj_id);
    }

    for (unsigned int s_i = 0; s_i < pre_sub_num; s_i++) {
      auto pre_id = pre_sub_list[s_i * 2];
      auto sub_id = pre_sub_list[s_i * 2 + 1];
      if (pre_id == this->type_pre_id_)
        continue;
      if (sub_id < Util::LITERAL_FIRST_ID && sub_id < this->entity_num_
          && this->entities_type_[sub_id] != TypeInvalid)
        unique_ids.insert(sub_id);
    }
    
    const size_t ori_nei_num = unique_ids.size();
    size_t nei_num = ori_nei_num;
    
    if (nei_num >= 10000) {
      kick_out_neighbour_nums++;
      for (auto one_hop_node : unique_ids) {
        auto &farthest_range = infos[one_hop_node]->farthest_range;
        if (farthest_range == no_limit_range) {
          farthest_range = 1;
          infos[one_hop_node]->see_all_range = 0;
        }
      }
      
      infos[i]->farthest_range = 0;
      infos[i]->see_all_range = 0;
      unique_ids.clear();
      delete[] pre_obj_list;
      delete[] pre_sub_list;
      continue;
    } else {
      CostEstimation estimated_cost;
      estimated_cost.id = i;
      for (unsigned int hop_i = 1; hop_i <= (D_HOP / 2); hop_i *= 2) {
        estimated_cost.cost += nei_num;
        nei_num *= avg_degree;
      }
      auto attr_num = this->CountAttrNum(unique_ids);
      estimated_cost.cost *= attr_num * (sizeof(double) + sizeof(TopTwoInfo) + sizeof(map<int, int>));
      memory_max_heap->push(estimated_cost);
      memory_now += estimated_cost.cost;
      
      while (memory_avaiable <= memory_now) {
        
        auto kick_out_item = memory_max_heap->popMax();
        auto k_id = kick_out_item.id;
        
        infos[k_id]->farthest_range = 0;
        infos[k_id]->see_all_range = 0;
        left_behind_kick_ids.insert(k_id);
        memory_now -= kick_out_item.cost;
        kick_out_time++;
      }
    }

    neighbour_list.push_back(pair_template);
    neighbour_list.back().first = i;
    auto &neighbours = neighbour_list.back().second;
    neighbours.reserve(unique_ids.size());
    for (auto uid : unique_ids)
      neighbours.push_back(uid);
    unique_ids.clear();
    delete[] pre_obj_list;
    delete[] pre_sub_list;
  }

  cout << " Preprocess 1-hop neighbour nodes used " << Util::get_cur_time() - t1 << " ms." << endl;
  t1 = Util::get_cur_time();

  
  for (auto id_one_hop_it = neighbour_list.begin(); id_one_hop_it != neighbour_list.end();) {
    auto node_id = id_one_hop_it->first;
    auto &neighbours = id_one_hop_it->second;
    if (infos[node_id]->farthest_range != 0 || left_behind_kick_ids.find(node_id) == left_behind_kick_ids.end()) {
      id_one_hop_it++;
      continue;
    }
    
    for (auto one_hop_node : neighbours) {
      auto &farthest_range = infos[one_hop_node]->farthest_range;
      if (farthest_range == no_limit_range) {
        farthest_range = 1;
        infos[one_hop_node]->see_all_range = 0;
      }
    }
    id_one_hop_it = neighbour_list.erase(id_one_hop_it);
  }

  
  unique_ids = std::set<TYPE_ENTITY_LITERAL_ID >();

  cout << "kick out "<<kick_out_neighbour_nums<<" at instance because its 1-hop size"<<endl;
  cout << "kick out "<<kick_out_time<<" times because its space cost"<<endl;
  cout<<"terminated_added:"<<terminated_added<<endl;
  cout<<"memory now:"<<memory_now<<endl;
  
  memory_max_heap.reset(nullptr);
  left_behind_kick_ids = std::set<TYPE_ENTITY_LITERAL_ID >();

  
  for (unsigned int k = 1; k <= D_HOP; k++)
    for (auto &id_one_hop_it : neighbour_list) {
      auto i = id_one_hop_it.first;
      auto its_farthest = infos[i]->farthest_range;
      auto its_see_all = infos[i]->see_all_range;
      
      if (its_farthest == k && its_see_all == k - 1) {
        for (auto nei : id_one_hop_it.second) {
          auto &nei_farthest = infos[nei]->farthest_range;
          if (nei_farthest == no_limit_range) {
            infos[nei]->farthest_range = k + 1;
            infos[nei]->see_all_range = k;
          }
        }
      }
    }

  
  for(auto & id_one_hop_it : neighbour_list) {
    auto node_id = id_one_hop_it.first;
    auto my_type = this->entities_type_[node_id];
    auto &neighbour = id_one_hop_it.second;
    for (auto nei:neighbour) {
      infos[nei]->one_hop_neighbour[my_type].insert(node_id);
    }
  }

  cout<<" Message Spreading used "<<Util::get_cur_time()-t1<<" ms."<<endl;
  t1 = Util::get_cur_time();

  long counter =0;
  
  
  for(auto & id_one_hop_it : neighbour_list)
  {
    auto i = id_one_hop_it.first;
    if( (counter +1) %5000000 == 0)
    {
      cout<<"\"Build Merged Info BuildHopInfo node "<<counter + 1;
      cout<<" used "<< Util::get_cur_time() - t1 << "ms. "<<endl;
    }
    counter++;

    if(this->entities_type_[i]==TypeInvalid || infos[i]->see_all_range == 0)
      continue;

    auto& merged_whole_node = infos[i]->merged_one_hop;
    auto& i_one_hop_neighbour = infos[i]->one_hop_neighbour;
    for(auto it=i_one_hop_neighbour.begin();it!= i_one_hop_neighbour.end();it++){
      auto one_hop_nodes_type = it->first;
      auto& one_hop_nodes = it->second;
      
      auto type_merged_node = this->MergeSameTypeNodes(one_hop_nodes);
      merged_whole_node[one_hop_nodes_type] = std::move(type_merged_node);
    }
  }


  cout<<" First Run Build Merged Info BuildHopInfo used "<<Util::get_cur_time()-t1<<" ms."<<endl;
  t1 = Util::get_cur_time();

  counter =0;
  
  
  for(const auto& id_and_one_hop:neighbour_list)
  {
    if( (counter +1) %5000000 == 0)
    {
      cout<<"second Run BuildHopInfo node "<<counter + 1;
      cout<<" used "<< Util::get_cur_time() - t1 << "ms. "<<endl;
    }
    counter++;

    auto node_id = id_and_one_hop.first;
    auto its_farthest = infos[node_id]->farthest_range;
    auto its_see_all = infos[node_id]->see_all_range;
    
    if(its_see_all != no_limit_range && its_see_all < 1)
      continue;

    for(const auto& id_neighbour : id_and_one_hop.second)
    {
      if(infos[id_neighbour]->see_all_range!=no_limit_range && infos[id_neighbour]->see_all_range<2)
        continue;
      auto &this_1hop_neighbours = infos[node_id]->one_hop_neighbour;
      auto &this_merged_1hop = infos[node_id]->merged_one_hop;
      const auto c_end = this_1hop_neighbours.cend();
      
      auto &nei_2hop_neighbours = infos[id_neighbour]->two_hop_neighbour;

      for(auto it = this_1hop_neighbours.cbegin(); it!=c_end; it++){
        auto node_type = it->first;
        const auto& nodes = it->second;
        
        auto &type_merged_node = this_merged_1hop[node_type];

        
        if(nei_2hop_neighbours.find(node_type) == nei_2hop_neighbours.end())
          nei_2hop_neighbours[node_type] = std::set<TYPE_ENTITY_LITERAL_ID>();
        for(auto one_hop_node:nodes)
          nei_2hop_neighbours[node_type].insert(one_hop_node);

        auto &id_neighbour_merged_info_2_hop=infos[id_neighbour]->merged_info_2_hop;
        if(id_neighbour_merged_info_2_hop.find(node_type) ==id_neighbour_merged_info_2_hop.end() )
          id_neighbour_merged_info_2_hop[node_type] = *type_merged_node;
        else 
        {
          
          auto &target_info = id_neighbour_merged_info_2_hop[node_type];
          const auto end_it =  type_merged_node->cend();
          for (auto cit = type_merged_node->cbegin();cit!=end_it;cit++)
          {
            auto predicate = cit->first;
            const auto &top_two_info = cit->second;
            target_info[predicate].AddAnotherNode(top_two_info);
          }
        }
      }
    }
  }


  
  for(TYPE_ENTITY_LITERAL_ID i = 0;i<this->entity_num_;i++) {
#ifdef HOP_INDEX_DEBUG_INFO
    if( (i +1) %5000000 == 0)
    {
      cout<<"check write its two-hop info node "<<i + 1;
      cout<<" used "<< Util::get_cur_time() - t1 << "ms. "<<endl;
    }
#endif
    auto node_type = this->entities_type_[i];
    if (node_type==TypeInvalid)
      continue;
    auto i_type = this->entities_type_[i];
    auto id_offset = this->entity_offset[i];
    auto i_predicate_num = this->predicates_num[i];
    auto its_see_all = infos[i]->see_all_range;

    if(its_see_all == no_limit_range || its_see_all >= 2 )
    {

      for (unsigned int i_predicate = 0; i_predicate < i_predicate_num; i_predicate++) {
        auto pre_id = this->predicate_ids[id_offset + i_predicate];
        
        auto min_max_value = infos[i]->merged_info_2_hop[i_type][pre_id].GetRange(i);
        auto has_min = get<0>(min_max_value);
        auto has_max = get<2>(min_max_value);
        if (has_min) {
          auto min_v = get<1>(min_max_value);
          this->min_values_[(id_offset+i_predicate)*D_HOP_LOG + 1] = min_v;
          this->mask_[(id_offset+i_predicate)*D_HOP_LOG + 1] = RangeState::Exact;
        }
        if (has_max) {
          auto max_v = get<3>(min_max_value);
          this->max_values_[(id_offset+i_predicate)*D_HOP_LOG + 1] = max_v;
          this->mask_[(id_offset+i_predicate)*D_HOP_LOG + 1] = RangeState::Exact;
        }
      }
    }
    else if(its_see_all != no_limit_range)
    {
      for (unsigned int i_predicate = 0; i_predicate < i_predicate_num; i_predicate++) {
        if(its_see_all<2)
          this->mask_[(id_offset+i_predicate)*D_HOP_LOG + 1] = RangeState::UnExact;
        if(its_see_all<4)
          this->mask_[(id_offset+i_predicate)*D_HOP_LOG + 2] = RangeState::UnExact;
      }
    }
  }

  cout <<"Second run merged 2 hop info used "<<Util::get_cur_time()-t1<<" ms."<<endl;
  t1 = Util::get_cur_time();

  
  
  for(TYPE_ENTITY_LITERAL_ID i = 0;i<this->entity_num_;i++) {
#ifdef HOP_INDEX_DEBUG_INFO
    if( (i +1) %5000000 == 0)
    {
      cout<<"each node will be notified its 4-hop info node "<<i + 1;
      cout<<" used "<< Util::get_cur_time() - t1 << "ms. "<<endl;
    }
#endif
    auto node_type = this->entities_type_[i];
    if (node_type==TypeInvalid)
      continue;

    
    if(infos[i]->see_all_range!=no_limit_range && infos[i]->see_all_range<2)
      continue;

    auto &i_two_hop_neighbour = infos[i]->two_hop_neighbour;
    const auto end_it = i_two_hop_neighbour.cend();
    for(auto it = i_two_hop_neighbour.cbegin(); it!=end_it; it++)
    {
      auto type_id = it->first;
      const auto &nodes = it->second;
      auto &merged_values = infos[i]->merged_info_2_hop[type_id];
      for(auto node_id : nodes) {
        if(infos[node_id]->see_all_range<4)
          continue;
        
        
        auto id_offset = this->entity_offset[i];
        auto i_predicate_num = this->predicates_num[i];
        for (unsigned int i_predicate = 0; i_predicate < i_predicate_num; i_predicate++) {
          auto pre_id = this->predicate_ids[id_offset + i_predicate];
          auto min_max_value = merged_values[pre_id].GetRange(node_id);
          auto has_min = get<0>(min_max_value);
          auto has_max = get<2>(min_max_value);
          if (has_min) {
            auto min_v = get<1>(min_max_value);
            this->min_values_[(id_offset+i_predicate)*D_HOP_LOG + 2] = min_v;
            this->mask_[(id_offset+i_predicate)*D_HOP_LOG + 2] = RangeState::Exact;
          }
          if (has_max) {
            auto max_v = get<3>(min_max_value);
            this->max_values_[(id_offset+i_predicate)*D_HOP_LOG + 2] = max_v;
            this->mask_[(id_offset+i_predicate)*D_HOP_LOG + 2] = RangeState::Exact;
          }
        }
      }
    }
  }

  cout <<"Third run used "<<Util::get_cur_time()-t1<<" ms."<<endl;
  t1 = Util::get_cur_time();
}


void HopIndex::Build(KVstore *kv_store)
{
  this->BuildNodeInfo(kv_store);
  this->BuildHopInfo(kv_store);
  this->loaded = true;
}

void HopIndex::Save(std::string file_name) {
  if(!this->loaded)
    return;
  cout<< "Save HopIndex to "<<file_name<<endl;
  auto f = fopen(file_name.c_str(), "wb");
  if (f == nullptr)
  {
    cout << "HopIndex::Save Error in open " << file_name << endl;
    perror("fopen");
    exit(0);
  }
  auto fd  = fileno(f);
  decltype(sizeof(this->entity_num_)) offset = 0;
  pwrite(fd, &(this->entity_num_), sizeof(this->entity_num_), offset);
  offset += sizeof(this->entity_num_);
  pwrite(fd, &(this->predicate_num_), 1 * sizeof(this->predicate_num_), offset);
  offset += sizeof(this->predicate_num_);
  pwrite(fd, &(this->type_pre_id_), 1 * sizeof(this->type_pre_id_), offset);
  offset += sizeof(this->type_pre_id_);

  pwrite(fd,this->entities_type_.get(),this->entity_num_*sizeof(TYPE_ENTITY_LITERAL_ID),offset);
  offset += this->entity_num_*sizeof(TYPE_ENTITY_LITERAL_ID);

  unsigned int value_type_predicate_size = this->predicates_to_number_.size();
  pwrite(fd,&value_type_predicate_size,sizeof(value_type_predicate_size),offset);
  offset += sizeof(value_type_predicate_size);

  for(auto pre_id : this->predicates_to_number_)
  {
    pwrite(fd,&pre_id,sizeof(TYPE_PREDICATE_ID),offset);
    offset += sizeof(TYPE_PREDICATE_ID);
  }
  pwrite(fd,&predicate_total,sizeof(predicate_total),offset);
  offset += sizeof(predicate_total);

  pwrite(fd,entity_offset.get(),sizeof(size_t)*entity_num_,offset);
  offset += sizeof(size_t)*entity_num_;

  pwrite(fd,predicates_num.get(),sizeof(size_t)*entity_num_,offset);
  offset += sizeof(size_t)*entity_num_;

  pwrite(fd,predicate_ids.get(),sizeof(TYPE_PREDICATE_ID)*predicate_total,offset);
  offset += sizeof(TYPE_PREDICATE_ID)*predicate_total;

  pwrite(fd,corresponding_nodes.get(),sizeof(TYPE_ENTITY_LITERAL_ID)*predicate_total,offset);
  offset += sizeof(TYPE_ENTITY_LITERAL_ID)*predicate_total;

  pwrite(fd,min_values_.get(),sizeof(double)*predicate_total*D_HOP_LOG,offset);
  offset += sizeof(double)*predicate_total*D_HOP_LOG;

  pwrite(fd,max_values_.get(),sizeof(double)*predicate_total*D_HOP_LOG,offset);
  offset += sizeof(double)*predicate_total*D_HOP_LOG;

  pwrite(fd,mask_.get(),sizeof(RangeState)*predicate_total*D_HOP_LOG,offset);
  offset += sizeof(RangeState)*predicate_total*D_HOP_LOG;

  fclose(f);
}

void HopIndex::Load(std::string file_name) {
  auto f = fopen(file_name.c_str(), "rb");
  if (f == nullptr)
  {
    cout << "HopIndex::Load cannot open " << file_name << " give up loading"<< endl;
    return;
  }
  auto fd  = fileno(f);
  decltype(sizeof(this->entity_num_)) offset = 0;
  pread(fd, &(this->entity_num_), sizeof(this->entity_num_), offset);
  offset += sizeof(this->entity_num_);
  pread(fd, &(this->predicate_num_), 1 * sizeof(this->predicate_num_), offset);
  offset += sizeof(this->predicate_num_);
  pread(fd, &(this->type_pre_id_), 1 * sizeof(this->type_pre_id_), offset);
  offset += sizeof(this->type_pre_id_);

  this->entities_type_.reset(new TYPE_ENTITY_LITERAL_ID[this->entity_num_]);
  pread(fd,this->entities_type_.get(),this->entity_num_*sizeof(TYPE_ENTITY_LITERAL_ID),offset);
  offset += this->entity_num_*sizeof(TYPE_ENTITY_LITERAL_ID);

  unsigned int value_type_predicate_size;
  pread(fd,&value_type_predicate_size,sizeof(value_type_predicate_size),offset);
  offset += sizeof(value_type_predicate_size);

  for(unsigned int i = 0; i< value_type_predicate_size;i++)
  {
    TYPE_PREDICATE_ID pre_id;
    pread(fd,&pre_id,sizeof(TYPE_PREDICATE_ID),offset);
    this->predicates_to_number_.insert(pre_id);
    offset += sizeof(TYPE_PREDICATE_ID);
  }

  pread(fd,&predicate_total,sizeof(predicate_total),offset);
  offset += sizeof(predicate_total);

  this->entity_offset = std::unique_ptr<size_t[]>(new size_t[entity_num_]);
  pread(fd,entity_offset.get(),sizeof(size_t)*entity_num_,offset);
  offset += sizeof(size_t)*entity_num_;

  this->predicates_num = std::unique_ptr<size_t[]>(new size_t[entity_num_]);
  pread(fd,predicates_num.get(),sizeof(size_t)*entity_num_,offset);
  offset += sizeof(size_t)*entity_num_;

  this->predicate_ids = std::unique_ptr<TYPE_PREDICATE_ID[]>(new TYPE_PREDICATE_ID[predicate_total]);
  pread(fd,predicate_ids.get(),sizeof(TYPE_PREDICATE_ID)*predicate_total,offset);
  offset += sizeof(TYPE_PREDICATE_ID)*predicate_total;

  this->corresponding_nodes = std::unique_ptr<TYPE_ENTITY_LITERAL_ID[]>(new TYPE_ENTITY_LITERAL_ID[predicate_total]);
  pread(fd,corresponding_nodes.get(),sizeof(TYPE_ENTITY_LITERAL_ID)*predicate_total,offset);
  offset += sizeof(TYPE_ENTITY_LITERAL_ID)*predicate_total;

  this->min_values_ = std::unique_ptr<double[]>(new double[predicate_total*D_HOP_LOG]);
  pread(fd,min_values_.get(),sizeof(double)*predicate_total*D_HOP_LOG,offset);
  offset += sizeof(double)*predicate_total*D_HOP_LOG;

  this->max_values_ = std::unique_ptr<double[]>(new double[predicate_total*D_HOP_LOG]);
  pread(fd,max_values_.get(),sizeof(double)*predicate_total*D_HOP_LOG,offset);
  offset += sizeof(double)*predicate_total*D_HOP_LOG;

  this->mask_ = std::unique_ptr<RangeState[]>(new RangeState[predicate_total*D_HOP_LOG]);
  pread(fd,mask_.get(),sizeof(RangeState)*predicate_total*D_HOP_LOG,offset);
  offset += sizeof(RangeState)*predicate_total*D_HOP_LOG;

  fclose(f);
  this->loaded = true;
  cout << "HopIndex::Load "<<file_name<<" done!"<<endl;
}

void TopTwoInfo::AddMaxNode(TYPE_ENTITY_LITERAL_ID id, double max_v) {
  if(id == this->max_1_node || id ==this->max_2_node)
    return ;
  auto max_v_should_inserted = this->max_2_node == INVALID_ENTITY_LITERAL_ID || this->max_2 < max_v;
  if(max_v_should_inserted)
  {
    auto insert_into_1 = this->max_1_node == INVALID_ENTITY_LITERAL_ID || this->max_1 < max_v;
    if(insert_into_1)
    {
      this->max_2 = this->max_1;
      this->max_2_node = this->max_1_node;
      this->max_1 = max_v;
      this->max_1_node = id;
    }
    else
    {
      this->max_2 = max_v;
      this->max_2_node = id;
    }
  }
}

void TopTwoInfo::AddMinNode(TYPE_ENTITY_LITERAL_ID id, double min_v) {
  if(id == this->min_1_node || id ==this->min_2_node)
    return ;
  auto min_v_should_inserted = this->min_2_node == INVALID_ENTITY_LITERAL_ID || this->min_2 > min_v;
  if(min_v_should_inserted)
  {
    auto insert_into_1 = this->min_1_node == INVALID_ENTITY_LITERAL_ID || this->min_1 > min_v;
    if(insert_into_1)
    {
      this->min_2 = this->min_1;
      this->min_2_node = this->min_1_node;
      this->min_1 = min_v;
      this->min_1_node = id;
    }
    else
    {
      this->min_2 = min_v;
      this->min_2_node = id;
    }
  }
}

void TopTwoInfo::AddAnotherNode(const TopTwoInfo &other) {
  if(other.min_1_node != INVALID_ENTITY_LITERAL_ID)
    this->AddMinNode(other.min_1_node ,other.min_1);
  if(other.min_2_node != INVALID_ENTITY_LITERAL_ID)
    this->AddMinNode(other.min_2_node ,other.min_2);

  if(other.max_1_node != INVALID_ENTITY_LITERAL_ID)
    this->AddMaxNode(other.max_1_node ,other.max_1);
  if(other.max_2_node != INVALID_ENTITY_LITERAL_ID)
    this->AddMaxNode(other.max_2_node ,other.max_2);

}





std::tuple<bool,double,bool,double> TopTwoInfo::GetRange(TYPE_ENTITY_LITERAL_ID id) const {
  bool has_min = false;
  double min_v;
  bool has_max = false;
  double max_v;

  if(this->min_1_node != INVALID_ENTITY_LITERAL_ID)
  {
    if (id == this->min_1_node)
    {
      if(this->min_2_node != INVALID_ENTITY_LITERAL_ID)
      {
        has_min = true;
        min_v = this->min_2;
      }
    }
    else
    {
      has_min = true;
      min_v = this->min_1;
    }
  }

  if(this->max_1_node != INVALID_ENTITY_LITERAL_ID)
  {
    if (id == this->max_1_node)
    {
      if(this->max_2_node != INVALID_ENTITY_LITERAL_ID)
      {
        has_max = true;
        max_v = this->max_2;
      }
    }
    else
    {
      has_max = true;
      max_v = this->max_1;
    }
  }
  return make_tuple(has_min,min_v,has_max,max_v);
}

