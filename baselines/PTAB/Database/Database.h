







#ifndef _DATABASE_DATABASE_H
#define _DATABASE_DATABASE_H 

#include "../Util/Util.h"
#include "../Util/Triple.h"
#include "Join.h"
#include "../Query/IDList.h"
#include "../Query/ResultSet.h"
#include "../Query/SPARQLquery.h"
#include "../Query/BasicQuery.h"
#include "../Signature/SigEntry.h"
#include "../VSTree/VSTree.h"
#include "../KVstore/KVstore.h"
#include "../StringIndex/StringIndex.h"
#include "../Parser/DBparser.h"
#include "../Parser/RDFParser.h"

#include "../Parser/SPARQL/SPARQLParser.h"
#include "../Query/QueryCache.h"
#include "../Query/GeneralEvaluation.h"
#include "CSR.h"
#include "./Statistics.h"

class Database
{
public:
	
	

	
	
	
	
	

	
	
	static const int STRING_MODE = 1;
	static const int ID_MODE = 2;
	CSR *csr;
	Database();
	Database(std::string _name);
	void release(FILE* fp0);
	~Database();

	bool save();
	bool load(bool loadCSR=false,bool load_cache=false,bool load_hop_index = true);
	bool unload();
	void clear();
  void setEmbeddingPrecomputeTime(long time_ms);
  int query(const string _query, ResultSet& _result_set, FILE* _fp = stdout,
            bool update_flag = true, bool export_flag = false,
            shared_ptr<Transaction> txn = nullptr,TopKStrategy top_k_method=TopKStrategy::DP_B);
	
	
	
	

	bool build(const string& _rdf_file);
	bool BuildHopIndex();
    bool LoadHopIndex(bool load_hop_index);
	
	bool insert(std::string _rdf_file, bool _is_restore = false, shared_ptr<Transaction> txn = nullptr);
	bool remove(std::string _rdf_file, bool _is_restore = false, shared_ptr<Transaction> txn = nullptr);

	bool backup();
	bool restore();

	
	string getName();
	
	TYPE_TRIPLE_NUM getTripleNum();
	TYPE_ENTITY_LITERAL_ID getEntityNum();
	TYPE_ENTITY_LITERAL_ID getLiteralNum();
	TYPE_ENTITY_LITERAL_ID getSubNum();
	TYPE_PREDICATE_ID getPreNum();

	
	string getSixTuplesFile();

	
	

	
	string getDBInfoFile();

	
	string getIDTuplesFile();

	
	KVstore* getKVstore();
	StringIndex* getStringIndex();
	QueryCache* getQueryCache();
	TYPE_TRIPLE_NUM* getpre2num();
	TYPE_TRIPLE_NUM* getpre2sub();
	TYPE_TRIPLE_NUM* getpre2obj();
	TYPE_ENTITY_LITERAL_ID& getlimitID_literal();
	TYPE_ENTITY_LITERAL_ID& getlimitID_entity();
	TYPE_PREDICATE_ID& getlimitID_predicate();
	mutex& get_query_parse_lock();
	
	
	void transaction_rollback(shared_ptr<Transaction> txn);
	void transaction_commit(shared_ptr<Transaction> txn);
	void version_clean();
private:
	string name;
	string store_path;
	bool is_active;
	atomic<TYPE_TRIPLE_NUM> triples_num;
	TYPE_ENTITY_LITERAL_ID entity_num;
	atomic<TYPE_ENTITY_LITERAL_ID> sub_num;
	
	TYPE_PREDICATE_ID pre_num;
	TYPE_ENTITY_LITERAL_ID literal_num;

	int encode_mode;
	bool if_loaded;
	long embedding_precompute_time_ms;

	
	mutex query_parse_lock;
	
	pthread_rwlock_t update_lock;
	
	mutex debug_lock;
	
	mutex getFinalResult_lock;
	
	mutex allocEntityID_lock;
	
	mutex allocLiteralID_lock;
	
	mutex allocPredicateID_lock;
	
	
	KVstore* kvstore;
	StringIndex* stringindex;
	unique_ptr<HopIndex> hop_index;
	Join* join;

    Statistics *statistics;

    
	string db_info_file;

	
	string six_tuples_file;

	
	string signature_binary_file;

	
	string id_tuples_file;
	string update_log;
	string update_log_since_backup;

	
	TYPE_TRIPLE_NUM* pre2num;
	
	TYPE_TRIPLE_NUM* pre2sub;
	
	TYPE_TRIPLE_NUM* pre2obj;
	
	TYPE_PREDICATE_ID maxNumPID, minNumPID;
	void setPreMap();

	
	
	Buffer* entity_buffer;
	
	unsigned entity_buffer_size;
	Buffer* literal_buffer;
	unsigned literal_buffer_size;

	QueryCache *query_cache;

	

	void setStringBuffer();
	void warmUp();
	
	

	void query_stringIndex(int id);
	void check();
	
	
	void load_entity2id(int _mode);
	void load_id2entity(int _mode);
	void load_literal2id(int _mode);
	void load_id2literal(int _mode);
	void load_predicate2id(int _mode);
	void load_id2predicate(int _mode);
	void load_sub2values(int _mode);
	void load_obj2values(int _mode);
	void load_pre2values(int _mode);
	
	
	void load_cache();
	void get_important_preID();
	std::vector <TYPE_PREDICATE_ID> important_preID;
	void load_important_sub2values();
	void load_important_obj2values();
	void load_candidate_pre2values();
	void build_CacheOfPre2values();
	void build_CacheOfSub2values();
	void build_CacheOfObj2values();
	void get_important_subID();
	void get_important_objID();
	void get_candidate_preID();
	std::priority_queue <KEY_SIZE_VALUE> candidate_preID;
	std::priority_queue <KEY_SIZE_VALUE> important_subID;
	std::priority_queue <KEY_SIZE_VALUE> important_objID;
	
	
	
	
	
	static const TYPE_ENTITY_LITERAL_ID START_ID_NUM = 0;
	
	
	
	string free_id_file_entity; 
	TYPE_ENTITY_LITERAL_ID limitID_entity; 
	BlockInfo* freelist_entity; 
	TYPE_ENTITY_LITERAL_ID allocEntityID();
	void freeEntityID(TYPE_ENTITY_LITERAL_ID _id);
	
	
	string free_id_file_literal;
	TYPE_ENTITY_LITERAL_ID limitID_literal;
	BlockInfo* freelist_literal;
	TYPE_ENTITY_LITERAL_ID allocLiteralID();
	void freeLiteralID(TYPE_ENTITY_LITERAL_ID _id);
	
	
	string free_id_file_predicate;
	TYPE_PREDICATE_ID limitID_predicate;
	BlockInfo* freelist_predicate;
	TYPE_PREDICATE_ID allocPredicateID();
	void freePredicateID(TYPE_PREDICATE_ID _id);
	
	void initIDinfo();  
	void resetIDinfo(); 
	void readIDinfo();  
	void writeIDinfo(); 
	void saveIDinfo(); 

	bool saveDBInfoFile();
	bool loadDBInfoFile();

	string getStorePath();

	
	

	
	
	
	
	
	
	

	

	
	
	bool exist_triple(TYPE_ENTITY_LITERAL_ID _sub_id, TYPE_PREDICATE_ID _pre_id, TYPE_ENTITY_LITERAL_ID _obj_id, shared_ptr<Transaction> txn = nullptr);
	bool exist_triple(const TripleWithObjType& _triple, shared_ptr<Transaction> txn = nullptr);

	
	
	
	
	
	
	
	
	
	bool encodeRDF_new(const string _rdf_file);
	void readIDTuples(ID_TUPLE*& _p_id_tuples);
	void build_s2xx(ID_TUPLE*);
	void build_o2xx(ID_TUPLE*);
	void build_p2xx(ID_TUPLE*);

    void load_statistics();

    
	
	bool insertTriple(const TripleWithObjType& _triple, vector<unsigned>* _vertices = NULL, vector<unsigned>* _predicates = NULL, shared_ptr<Transaction> txn  = nullptr);
	bool removeTriple(const TripleWithObjType& _triple, vector<unsigned>* _vertices = NULL, vector<unsigned>* _predicates = NULL, shared_ptr<Transaction> txn = nullptr);
	
	unsigned insert(const TripleWithObjType* _triples, TYPE_TRIPLE_NUM _triple_num, bool _is_restore=false , shared_ptr<Transaction> txn = nullptr);
	
	unsigned remove(const TripleWithObjType* _triples, TYPE_TRIPLE_NUM _triple_num, bool _is_restore=false, shared_ptr<Transaction> txn = nullptr);

	bool sub2id_pre2id_obj2id_RDFintoSignature(const string _rdf_file);
	

	bool objIDIsEntityID(TYPE_ENTITY_LITERAL_ID _id);

	
	

	

	

	
	bool getFinalResult(SPARQLquery& _sparql_q, ResultSet& _result_set);

	static int read_update_log(const string _path, multiset<string>& _i, multiset<string>& _r);
	bool restore_update(multiset<string>& _i, multiset<string>& _r);
	void clear_update_log();
};

#endif 

