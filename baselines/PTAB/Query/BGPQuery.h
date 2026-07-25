


















#ifndef GSTORE_BGPQUERY_H
#define GSTORE_BGPQUERY_H

#include "BasicQuery.h"
#include "../Util/Util.h"
#include "../Util/Triple.h"
#include "../KVstore/KVstore.h"

#include <cstdlib>
#include <utility>
#include <vector>


using namespace std;



class VarDescriptor{

public:
	
	enum class VarType{Entity,Predicate};
	enum class EntiType{VarEntiType, ConEntiType};
	enum class ItemType{SubType, PreType, ObjType};
	enum class PreType{VarPreType, ConPreType};

	unsigned id_;
	VarType var_type_;
	std::string var_name_;

	
	bool selected_;
	bool link_with_const;

	
	
	unsigned degree_;

	
	
	


	vector<ItemType> so_var_item_type;
	
	vector<char> so_edge_type_;
	vector<unsigned> so_edge_index_;
	vector<unsigned> so_edge_nei_;
	vector<EntiType> so_edge_nei_type_;
	vector<unsigned> so_edge_pre_id_;
	vector<PreType> so_edge_pre_type_;

	
	
	
	
	
	
	


	
	
	
	
	
	
	


	
	vector<unsigned> s_id_;
	vector<EntiType> s_type_;
	vector<unsigned> o_id_;
	vector<EntiType> o_type_;
	vector<unsigned> pre_edge_index_;

	
	int rewriting_position_;
	
	int basic_query_id;

	
	
	

	VarDescriptor(unsigned id, VarType var_type, const string &var_name);


	
	
	

	void update_so_var_edge_info(unsigned edge_nei_id, TYPE_PREDICATE_ID pre_id, char edge_type,
								 unsigned edge_index, bool pre_is_var, bool edge_nei_is_var);

	void update_pre_var_edge_info(unsigned s_id, unsigned o_id, bool s_is_var, bool o_is_var, unsigned edge_index);

	void update_statistics_num();

	
	void update_select_status(bool selected);

	void print(KVstore *kvstore);

};


class SOVarDescriptor:public VarDescriptor{

};






class BGPQuery {
public:

	
	map<string, unsigned > item_to_freq;

	
	map<string, unsigned > var_item_to_position;
	map<string, unsigned > var_item_to_id;

	map<unsigned, unsigned> id_position_map;
	map<unsigned, unsigned> position_id_map;


	vector<unsigned> s_id_;
	vector<unsigned> p_id_;
	vector<unsigned> o_id_;

	vector<bool> s_is_constant_;
	vector<bool> p_is_constant_;
	vector<bool> o_is_constant_;

	
	vector<shared_ptr<VarDescriptor>> var_vector;

	
	vector<unsigned> so_var_id;
	vector<unsigned> pre_var_id;
	vector<unsigned> var_id_vec;

	
	vector<unsigned> selected_var_id;

	vector<string> pre_var_names;





	int selected_pre_var_num;
	int selected_so_var_num;
	int total_selected_var_num;

	unsigned total_pre_var_num;
	unsigned total_so_var_num;
	unsigned total_var_num;


	int join_pre_var_num;
	int join_so_var_num;
	int total_join_var_num;


	BGPQuery();
	void initial();


	void AddTriple(const Triple& _triple);

	
	unsigned get_var_id_by_name(const string& var_name);
	string get_var_name_by_id(unsigned var_id);
	unsigned get_var_position_by_name(const string& var_name);


	unsigned get_var_id_by_index(unsigned index);
	unsigned get_var_position_by_id(unsigned id);

	const vector<unsigned> &get_var_id_vec();

	const shared_ptr<VarDescriptor> &get_vardescrip_by_index(unsigned index);
	const shared_ptr<VarDescriptor> &get_vardescrip_by_id(unsigned id);

	

	void ScanAllVar(const vector<string>& _query_var);
	void build_edge_info(KVstore *_kvstore);
	void count_statistics_num();

	bool EncodeBGPQuery(KVstore* _kvstore, const vector<string>& _query_var);



	void ScanAllVarByBigBGPID(BGPQuery *big_bgpquery, const vector<string>& _query_var);
	bool EncodeSmallBGPQuery(BGPQuery *big_bgpquery_, KVstore* _kvstore, const vector<string>& _query_var);

	unsigned get_triple_num();
	unsigned get_total_var_num();
	unsigned get_pre_var_num();


	unsigned get_var_degree(unsigned var_id);
	VarDescriptor::VarType get_var_type_by_id(unsigned var_id);

	bool is_var_selected(unsigned var_id);

	
	unsigned get_so_var_edge_index(unsigned var_id, int edge_id);
	bool get_so_var_edge_type(unsigned var_id, unsigned edge_id);
	unsigned get_so_var_edge_nei(unsigned var_id, unsigned edge_id);
	VarDescriptor::EntiType get_so_var_edge_nei_type(unsigned var_id, unsigned edge_id);
	unsigned get_so_var_edge_pre_id(unsigned var_id, unsigned edge_id);
	VarDescriptor::PreType get_so_var_edge_pre_type(unsigned var_id, unsigned edge_id);


	
	unsigned get_pre_var_edge_index(unsigned var_id, unsigned edge_id);
	unsigned get_pre_var_s_id(unsigned var_id, unsigned edge_id);
	VarDescriptor::EntiType get_pre_var_s_type(unsigned var_id, unsigned edge_id);
	unsigned get_pre_var_o_id(unsigned var_id, unsigned edge_id);
	VarDescriptor::EntiType get_pre_var_o_type(unsigned var_id, unsigned edge_id);

	const vector<Triple> &get_triple_vt();
	const Triple &get_triple_by_index(unsigned index);


	bool is_var_satellite_by_index(unsigned index);

	void print(KVstore * kvstore);
    vector<unsigned*>* get_result_list_pointer();

private:
	vector<Triple> triple_vt;
    vector<unsigned*> result_list;

};


#endif 
