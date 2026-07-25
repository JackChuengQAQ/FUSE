







#include "Database.h"

using namespace std;

Database::Database()
{
	this->embedding_precompute_time_ms = 0;
	this->name = "";
	this->store_path = "";
	string store_path = ".";
	this->signature_binary_file = "signature.binary";
	this->six_tuples_file = "six_tuples";
	this->db_info_file = "db_info_file.dat";
	this->id_tuples_file = "id_tuples";
	this->update_log = "update.log";
	this->update_log_since_backup = "update_since_backup.log";
	

	string kv_store_path = store_path + "/kv_store";
	this->kvstore = new KVstore(kv_store_path);

	
	

	string stringindex_store_path = store_path + "/stringindex_store";
	this->stringindex = new StringIndex(stringindex_store_path);
	this->stringindex->SetTrie(this->kvstore->getTrie());
	
	this->encode_mode = Database::ID_MODE;
	this->is_active = false;
	this->sub_num = 0;
	this->pre_num = 0;
	this->literal_num = 0;
	this->entity_num = 0;
	this->triples_num = 0;

	this->join = NULL;
	this->pre2num = NULL;
	this->pre2sub = NULL;
	this->pre2obj = NULL;
	this->entity_buffer = NULL;
	this->entity_buffer_size = 0;
	this->literal_buffer = NULL;
	this->literal_buffer_size = 0;

	this->query_cache = new QueryCache();

	this->if_loaded = false;

	

	
	this->initIDinfo();

	pthread_rwlock_init(&(this->update_lock), NULL);
}

Database::Database(string _name)
{
	this->embedding_precompute_time_ms = 0;
	this->name = _name;
	size_t found = this->name.find_last_not_of('/');
	if (found != string::npos) 
	{
		this->name.erase(found + 1);
	}
	this->store_path = Util::global_config["db_home"] + "/" + this->name + Util::global_config["db_suffix"];

	this->signature_binary_file = "signature.binary";
	this->six_tuples_file = "six_tuples";
	this->db_info_file = "db_info_file.dat";
	this->id_tuples_file = "id_tuples";
	this->update_log = "update.log";
	this->update_log_since_backup = "update_since_backup.log";
	

	string kv_store_path = store_path + "/kv_store";
	this->kvstore = new KVstore(kv_store_path);
	
	
	
	string stringindex_store_path = store_path + "/stringindex_store";
	this->stringindex = new StringIndex(stringindex_store_path);
	this->stringindex->SetTrie(this->kvstore->getTrie());
	
	this->encode_mode = Database::ID_MODE;
	this->is_active = false;
	this->sub_num = 0;
	this->pre_num = 0;
	this->literal_num = 0;
	this->entity_num = 0;
	this->triples_num = 0;

	this->if_loaded = false;

	this->join = NULL;
	this->pre2num = NULL;
	this->pre2sub = NULL;
	this->pre2obj = NULL;
	this->entity_buffer = NULL;
	this->entity_buffer_size = 0;
	this->literal_buffer = NULL;
	this->literal_buffer_size = 0;

	this->query_cache = new QueryCache();

	

	
	this->initIDinfo();

	pthread_rwlock_init(&(this->update_lock), NULL);
}












void
Database::initIDinfo()
{
	
	this->free_id_file_entity = this->getStorePath() + "/freeEntityID.dat";
	this->limitID_entity = 0;
	this->freelist_entity = NULL;

	this->free_id_file_literal = this->getStorePath() + "/freeLiteralID.dat";
	this->limitID_literal = 0;
	this->freelist_literal = NULL;

	this->free_id_file_predicate = this->getStorePath() + "/freePredicateID.dat";
	this->limitID_predicate = 0;
	this->freelist_predicate = NULL;
}

void
Database::resetIDinfo()
{
	this->initIDinfo();

	
	
	
	

	
	
	
	
	
	
	
	
	
	
}

void
Database::readIDinfo()
{
	this->initIDinfo();

	FILE* fp = NULL;
	
	TYPE_ENTITY_LITERAL_ID t = INVALID_ENTITY_LITERAL_ID;
	BlockInfo* bp = NULL;

	fp = fopen(this->free_id_file_entity.c_str(), "r");
	if (fp == NULL)
	{
		cout << "read entity id info error" << endl;
		return;
	}
	
	
	
	BlockInfo *tmp = NULL, *cur = NULL;
	fread(&(this->limitID_entity), sizeof(int), 1, fp);
	fread(&t, sizeof(int), 1, fp);
	while (!feof(fp))
	{
		
		
		
		
		
		
		tmp = new BlockInfo(t);
		if (cur == NULL)
		{
			this->freelist_entity = cur = tmp;
		}
		else
		{
			cur->next = tmp;
			cur = tmp;
		}
		fread(&t, sizeof(int), 1, fp);
	}
	fclose(fp);
	fp = NULL;


	fp = fopen(this->free_id_file_literal.c_str(), "r");
	if (fp == NULL)
	{
		cout << "read literal id info error" << endl;
		return;
	}

	fread(&(this->limitID_literal), sizeof(int), 1, fp);
	fread(&t, sizeof(int), 1, fp);
	while (!feof(fp))
	{
		bp = new BlockInfo(t, this->freelist_literal);
		this->freelist_literal = bp;
		fread(&t, sizeof(int), 1, fp);
	}
	fclose(fp);
	fp = NULL;

	fp = fopen(this->free_id_file_predicate.c_str(), "r");
	if (fp == NULL)
	{
		cout << "read predicate id info error" << endl;
		return;
	}
	fread(&(this->limitID_predicate), sizeof(int), 1, fp);
	fread(&t, sizeof(int), 1, fp);
	while (!feof(fp))
	{
		bp = new BlockInfo(t, this->freelist_predicate);
		this->freelist_predicate = bp;
		fread(&t, sizeof(int), 1, fp);
	}
	fclose(fp);
	fp = NULL;
}

void
Database::writeIDinfo()
{
	
	FILE* fp = NULL;
	BlockInfo *bp = NULL, *tp = NULL;

	fp = fopen(this->free_id_file_entity.c_str(), "w+");
	if (fp == NULL)
	{
		cout << "write entity id info error" << endl;
		return;
	}
	fwrite(&(this->limitID_entity), sizeof(int), 1, fp);
	bp = this->freelist_entity;
	while (bp != NULL)
	{
		
		
		
		
		fwrite(&(bp->num), sizeof(int), 1, fp);
		tp = bp->next;
		delete bp;
		bp = tp;
	}
	fclose(fp);
	fp = NULL;

	fp = fopen(this->free_id_file_literal.c_str(), "w+");
	if (fp == NULL)
	{
		cout << "write literal id info error" << endl;
		return;
	}
	fwrite(&(this->limitID_literal), sizeof(int), 1, fp);
	bp = this->freelist_literal;
	while (bp != NULL)
	{
		fwrite(&(bp->num), sizeof(int), 1, fp);
		tp = bp->next;
		delete bp;
		bp = tp;
	}
	fclose(fp);
	fp = NULL;

	fp = fopen(this->free_id_file_predicate.c_str(), "w+");
	if (fp == NULL)
	{
		cout << "write predicate id info error" << endl;
		return;
	}
	fwrite(&(this->limitID_predicate), sizeof(int), 1, fp);
	bp = this->freelist_predicate;
	while (bp != NULL)
	{
		fwrite(&(bp->num), sizeof(int), 1, fp);
		tp = bp->next;
		delete bp;
		bp = tp;
	}
	fclose(fp);
	fp = NULL;
}

void
Database::saveIDinfo()
{
	
	FILE* fp = NULL;
	BlockInfo *bp = NULL, *tp = NULL;

	fp = fopen(this->free_id_file_entity.c_str(), "w+");
	if (fp == NULL)
	{
		cout << "write entity id info error" << endl;
		return;
	}
	fwrite(&(this->limitID_entity), sizeof(int), 1, fp);
	bp = this->freelist_entity;
	while (bp != NULL)
	{
		fwrite(&(bp->num), sizeof(int), 1, fp);
		tp = bp->next;
		bp = tp;
	}
	Util::Csync(fp);
	fclose(fp);
	fp = NULL;

	fp = fopen(this->free_id_file_literal.c_str(), "w+");
	if (fp == NULL)
	{
		cout << "write literal id info error" << endl;
		return;
	}
	fwrite(&(this->limitID_literal), sizeof(int), 1, fp);
	bp = this->freelist_literal;
	while (bp != NULL)
	{
		fwrite(&(bp->num), sizeof(int), 1, fp);
		tp = bp->next;
		bp = tp;
	}
	Util::Csync(fp);
	fclose(fp);
	fp = NULL;

	fp = fopen(this->free_id_file_predicate.c_str(), "w+");
	if (fp == NULL)
	{
		cout << "write predicate id info error" << endl;
		return;
	}
	fwrite(&(this->limitID_predicate), sizeof(int), 1, fp);
	bp = this->freelist_predicate;
	while (bp != NULL)
	{
		fwrite(&(bp->num), sizeof(int), 1, fp);
		tp = bp->next;
		bp = tp;
	}
	Util::Csync(fp);
	fclose(fp);
	fp = NULL;
}



TYPE_ENTITY_LITERAL_ID
Database::allocEntityID()
{
	allocEntityID_lock.lock();
	
	TYPE_ENTITY_LITERAL_ID t = INVALID_ENTITY_LITERAL_ID;

	if (this->freelist_entity == NULL)
	{
		t = this->limitID_entity++;
		if (this->limitID_entity >= Util::LITERAL_FIRST_ID)
		{
			cout << "fail to alloc id for entity" << endl;
			
			allocEntityID_lock.unlock();
			return INVALID;
		}
	}
	else
	{
		t = this->freelist_entity->num;
		BlockInfo* op = this->freelist_entity;
		this->freelist_entity = this->freelist_entity->next;
		delete op;
	}

	this->entity_num++;
	allocEntityID_lock.unlock();
	return t;
}

void
Database::freeEntityID(TYPE_ENTITY_LITERAL_ID _id)
{
	allocEntityID_lock.lock();
	if (_id == this->limitID_entity - 1)
	{
		this->limitID_entity--;
	}
	else
	{
		BlockInfo* p = new BlockInfo(_id, this->freelist_entity);
		this->freelist_entity = p;
	}

	this->entity_num--;
	allocEntityID_lock.unlock();
}

TYPE_ENTITY_LITERAL_ID
Database::allocLiteralID()
{
	
	allocLiteralID_lock.lock();
	TYPE_ENTITY_LITERAL_ID t = INVALID_ENTITY_LITERAL_ID;

	if (this->freelist_literal == NULL)
	{
		t = this->limitID_literal++;
		if (this->limitID_literal >= Util::LITERAL_FIRST_ID)
		{
			cout << "fail to alloc id for literal" << endl;
			
			allocLiteralID_lock.unlock();
			return INVALID;
		}
	}
	else
	{
		t = this->freelist_literal->num;
		BlockInfo* op = this->freelist_literal;
		this->freelist_literal = this->freelist_literal->next;
		delete op;
	}

	this->literal_num++;
	allocLiteralID_lock.unlock();
	return t + Util::LITERAL_FIRST_ID;
}

void
Database::freeLiteralID(TYPE_ENTITY_LITERAL_ID _id)
{
	allocLiteralID_lock.lock();
	_id -= Util::LITERAL_FIRST_ID;

	if (_id == this->limitID_literal - 1)
	{
		this->limitID_literal--;
	}
	else
	{
		BlockInfo* p = new BlockInfo(_id, this->freelist_literal);
		this->freelist_literal = p;
	}

	this->literal_num--;
	allocLiteralID_lock.unlock();
}

TYPE_PREDICATE_ID
Database::allocPredicateID()
{
	
	allocPredicateID_lock.lock();
	TYPE_PREDICATE_ID t = INVALID_PREDICATE_ID;

	if (this->freelist_predicate == NULL)
	{
		t = this->limitID_predicate++;
		if (this->limitID_predicate >= Util::LITERAL_FIRST_ID)
		{
			cout << "fail to alloc id for predicate" << endl;
			
			allocPredicateID_lock.unlock();
			return -1;
		}
	}
	else
	{
		t = this->freelist_predicate->num;
		BlockInfo* op = this->freelist_predicate;
		this->freelist_predicate = this->freelist_predicate->next;
		delete op;
	}

	this->pre_num++;
	allocPredicateID_lock.unlock();
	return t;
}

void
Database::freePredicateID(TYPE_PREDICATE_ID _id)
{
	allocPredicateID_lock.lock();
	if (_id == this->limitID_predicate - 1)
	{
		this->limitID_predicate--;
	}
	else
	{
		BlockInfo* p = new BlockInfo(_id, this->freelist_predicate);
		this->freelist_predicate = p;
	}

	this->pre_num--;
	allocPredicateID_lock.unlock();
}

void
Database::release(FILE* fp0)
{
	fprintf(fp0, "begin to delete DB!\n");
	fflush(fp0);
	
	
	
	fflush(fp0);
	delete this->kvstore;
	fprintf(fp0, "ok to delete kvstore!\n");
	fflush(fp0);
	
	
	fprintf(fp0, "ok to delete DB!\n");
	fflush(fp0);
}

Database::~Database()
{
	pthread_rwlock_destroy(&(this->update_lock));
	this->unload();
	
	
}


void
Database::setPreMap()
{
	
	this->maxNumPID = this->minNumPID = INVALID_PREDICATE_ID;
	
	TYPE_TRIPLE_NUM max = 0, min = this->triples_num + 1;

	this->pre2num = new TYPE_TRIPLE_NUM[this->limitID_predicate];
	this->pre2sub = new TYPE_TRIPLE_NUM[this->limitID_predicate];
	this->pre2obj = new TYPE_TRIPLE_NUM[this->limitID_predicate];
	TYPE_PREDICATE_ID valid = 0, i, t;

	for (i = 0; i < this->limitID_predicate; ++i)
	{
		if (valid == this->pre_num)
		{
			t = 0;
		}
		else
		{
			t = this->kvstore->getPredicateDegree(i);
		}
		this->pre2num[i] = t;

		
		if (t > 0)
		{
			valid++;
			if (t > max)
			{
				this->maxNumPID = i;
			}
			if (t < min)
			{
				this->minNumPID = i;
			}

			unsigned *list = NULL;
			unsigned len = 0;
			this->kvstore->getsubIDlistBypreID(i,list,len,true);
			this->pre2sub[i] = len;
			free(list);
			this->kvstore->getobjIDlistBypreID(i,list,len,true);
			this->pre2obj[i] = len;
			free(list);
		}
		else
		{
			this->pre2sub[i] = 0;
			this->pre2obj[i] = 0;
		}
	}
	








}

void
Database::setStringBuffer()
{
	
	
	this->entity_buffer_size = (this->limitID_entity<50000000) ? this->limitID_entity : 50000000;
	this->literal_buffer_size = (this->limitID_literal<50000000) ? this->limitID_literal : 50000000;
	this->entity_buffer = new Buffer(this->entity_buffer_size);
	this->literal_buffer = new Buffer(this->literal_buffer_size);

	
	
	TYPE_ENTITY_LITERAL_ID valid = 0, i;
	string str;
	for (i = 0; i < this->entity_buffer_size; ++i)
	{
		if (valid == this->entity_num)
		{
			str = "";
		}
		else
		{
			str = this->kvstore->getEntityByID(i);
		}
		this->entity_buffer->set(i, str);
		if (str != "")
		{
			valid++;
		}
	}

	valid = 0;
	for (i = 0; i < this->literal_buffer_size; ++i)
	{
		if (valid == this->literal_num)
		{
			str = "";
		}
		else
		{
			str = this->kvstore->getLiteralByID(i + Util::LITERAL_FIRST_ID);
		}
		this->literal_buffer->set(i, str);
		if (str != "")
		{
			valid++;
		}
	}

	
	this->stringindex->setBuffer(this->entity_buffer, this->literal_buffer);
}

void
Database::warmUp()
{
	
	TYPE_PREDICATE_ID pid1 = this->maxNumPID;
	ResultSet rs1;
	string str1 = "select ?s ?o where { ?s " + this->kvstore->getPredicateByID(pid1) + " ?o . }";
	this->query(str1, rs1);
	
	TYPE_PREDICATE_ID pid2 = this->minNumPID;
	ResultSet rs2;
	string str2 = "select ?s ?o where { ?s " + this->kvstore->getPredicateByID(pid2) + " ?o . }";
	this->query(str2, rs2);
}

bool
Database::load(bool loadCSR,bool load_cache,bool load_hop_index)
{
	if(this->if_loaded)
	{
		return true;
	}

	
	
	unsigned vstree_cache = LRUCache::DEFAULT_CAPACITY;
	bool flag;
#ifndef THREAD_ON
	
	
	
		
		
	
	(this->kvstore)->open();
#else
	
	int kv_mode = KVstore::READ_WRITE_MODE;
	thread entity2id_thread(&Database::load_entity2id, this, kv_mode);
	thread id2entity_thread(&Database::load_id2entity, this, kv_mode);
	thread literal2id_thread(&Database::load_literal2id, this, kv_mode);
	thread id2literal_thread(&Database::load_id2literal, this, kv_mode);
	thread predicate2id_thread(&Database::load_predicate2id, this, kv_mode);
#ifndef ONLY_READ
	thread id2predicate_thread(&Database::load_id2predicate, this, kv_mode);
#endif
	thread sub2values_thread(&Database::load_sub2values, this, kv_mode);
	thread obj2values_thread(&Database::load_obj2values, this, kv_mode);
	thread pre2values_thread(&Database::load_pre2values, this, kv_mode);
#endif

	
	flag = this->loadDBInfoFile();
	if (!flag)
	{
		cout << "load database info error in Database::load()" << endl;
		return false;
	}

	if(!(this->kvstore)->load_trie(kv_mode))
		return false;

	this->stringindex->SetTrie(this->kvstore->getTrie());
	
	this->stringindex->load();
	this->readIDinfo();

#ifdef THREAD_ON
	pre2values_thread.join();
#endif
	this->setPreMap();

#ifdef THREAD_ON
	id2entity_thread.join();
	id2literal_thread.join();
#endif

	
	

	
	
	

	

	
	

#ifndef ONLY_READ
#ifdef THREAD_ON
	id2predicate_thread.join();
#endif
#endif

#ifdef THREAD_ON
	entity2id_thread.join();
	literal2id_thread.join();
	predicate2id_thread.join();
	sub2values_thread.join();
	obj2values_thread.join();
	
	
#endif
	
	if(load_cache)
	  this->load_cache();
	
	
	
	

	

	














    this->if_loaded = true;

	cout << "finish load" << endl;

	
	

	
	check();

#ifdef ONLY_READ
	this->kvstore->close_id2entity();
	this->kvstore->close_id2literal();
#endif

	if (loadCSR)
	{
		this->csr = new CSR[2];
		unsigned pre_num = this->getStringIndex()->getNum(StringIndexFile::Predicate);
		this->csr[0].init(pre_num);
		this->csr[1].init(pre_num);	
		cout<<"pre_num: "<<pre_num<<endl;
		long begin_time = Util::get_cur_time();

		
		
		for(unsigned i = 0;i<pre_num;i++)
		{
			string pre = (this->getKVstore())->getPredicateByID(i);
			cout<<"pid: "<<i<<"    pre: "<<pre<<endl;
			unsigned* sublist = NULL;
			unsigned sublist_len = 0;
			bool ret = (this->getKVstore())->getsubIDlistBypreID(i, sublist, sublist_len, true);
			
			unsigned offset = 0;
			unsigned index = 0;
			for(unsigned j=0;j<sublist_len;j++)
			{
				string sub = (this->getKVstore())->getEntityByID(sublist[j]);
				
				unsigned* objlist = NULL;
				unsigned objlist_len = 0;
				bool ret = (this->getKVstore())->getobjIDlistBysubIDpreID(sublist[j], i, objlist, objlist_len); 
				unsigned len = objlist_len;	
				for(unsigned k=0;k<objlist_len;k++)
				{
					if(objlist[k]>=2000000000)
					{
						--len;
						continue;
					}
					string obj = (this->getKVstore())->getEntityByID(objlist[k]);
					
					this->csr[0].adjacency_list[i].push_back(objlist[k]);
				}
				
				if(len > 0)
				{
					this->csr[0].id2vid[i].push_back(sublist[j]);
					this->csr[0].vid2id[i].insert(pair<unsigned, unsigned>(sublist[j], index));
					this->csr[0].offset_list[i].push_back(offset);
					index++;
					offset += len;
				}
			}
			
			
			
			
			
			
			
			
			
			cout<<this->csr[0].offset_list[i].size()<<endl;	
			cout<<this->csr[0].adjacency_list[i].size()<<endl;	
		}

		
		
		for(unsigned i = 0;i<pre_num;i++)
		{
			string pre = (this->getKVstore())->getPredicateByID(i);
			cout<<"pid: "<<i<<"    pre: "<<pre<<endl;
			unsigned* objlist = NULL;
			unsigned objlist_len = 0;
			bool ret = (this->getKVstore())->getobjIDlistBypreID(i, objlist, objlist_len, true);
			
			unsigned offset = 0;
			unsigned index = 0;
			for(unsigned j=0;j<objlist_len;j++)
			{
				if(objlist[j]>=2000000000)
					continue;
				string obj = (this->getKVstore())->getEntityByID(objlist[j]);
				
				unsigned* sublist = NULL;
				unsigned sublist_len = 0;
				bool ret = (this->getKVstore())->getsubIDlistByobjIDpreID(objlist[j], i, sublist, sublist_len); 
				unsigned len = sublist_len;
				for(unsigned k=0;k<sublist_len;k++)
				{
					string sub = (this->getKVstore())->getEntityByID(sublist[k]);
					
					this->csr[1].adjacency_list[i].push_back(sublist[k]);
				}
				
				if(len > 0)
				{
					this->csr[1].id2vid[i].push_back(objlist[j]);
					this->csr[1].vid2id[i].insert(pair<unsigned, unsigned>(objlist[j], index));
					this->csr[1].offset_list[i].push_back(offset);
					index++;
					offset += len;
				}
			}
			
			
			
			
			
			
			
			
			
			cout<<this->csr[1].offset_list[i].size()<<endl;
			cout<<this->csr[1].adjacency_list[i].size()<<endl;
		}
		long end_time = Util::get_cur_time();
		cout << "after creating CSR, used " << (end_time - begin_time) << "ms" << endl;
		cout << "CSR size = " << csr[0].sizeInBytes() + csr[1].sizeInBytes() << " (bytes)" << endl;
	}

	this->LoadHopIndex(load_hop_index);
	return true;
}

void
Database::load_cache()
{
	
	
	cout << "get important pre ID" << endl;
	this->get_important_preID();
	cout << "total preID num is " << pre_num << endl;
	cout << "important pre ID is: ";
	for(int i = 0; i < important_preID.size(); ++i)
		cout << important_preID[i] << ' ';
	cout << endl;
	
	
	
	
	long t0 = Util::get_cur_time();
	vector<StringIndexFile*> indexfile = this->stringindex->get_three_StringIndexFile();

	StringIndexFile* 	entity = indexfile[0];
	StringIndexFile* 	literal = indexfile[1];
	StringIndexFile* 	predicate = indexfile[2];

	struct stat statbuf;
	int fd;
	char tmp;
	long end;
	
	stat((entity->get_loc() + "value").c_str(), &statbuf);
	fd = open((entity->get_loc() + "value").c_str(), O_RDONLY);
	entity->mmapLength = (statbuf.st_size/4096 + 1)*4096;
	entity->Mmap = (char*)mmap(NULL, entity->mmapLength, PROT_READ, MAP_POPULATE|MAP_SHARED, fd, 0);
	close(fd);
	end = entity->mmapLength - 4096;
	for (long off = 0; off < end; off += 4096)
	{	
		tmp = entity->Mmap[off];
	}
	stat((literal->get_loc() + "value").c_str(), &statbuf);
	fd = open((literal->get_loc() + "value").c_str(), O_RDONLY);
	literal->mmapLength = (statbuf.st_size / 4096 + 1) * 4096;
	literal->Mmap = (char*)mmap(NULL, literal->mmapLength, PROT_READ, MAP_POPULATE | MAP_SHARED , fd, 0);
	close(fd);
	end = literal->mmapLength - 4096;
	for (long off = 0; off < end; off += 4096)
	{
		tmp = literal->Mmap[off];
	}
	stat((predicate->get_loc() + "value").c_str(), &statbuf);
	fd = open((predicate->get_loc() + "value").c_str(), O_RDONLY);
	predicate->mmapLength = (statbuf.st_size / 4096 + 1) * 4096;
	predicate->Mmap = (char*)mmap(NULL, predicate->mmapLength, PROT_READ, MAP_POPULATE | MAP_SHARED, fd, 0);
	close(fd);
	end = predicate->mmapLength - 4096;
	for (long off = 0; off < end; off += 4096)
	{
		tmp = predicate->Mmap[off];
	}
	cout << "Value File Preload used " << Util::get_cur_time() - t0 << " ms" << endl;
	
























}

void
Database::get_important_preID()
{
	important_preID.clear();
	unsigned max_degree = 0;
	for(TYPE_PREDICATE_ID i = 0; i < limitID_predicate; ++i)
		if (pre2num[i] > max_degree)
			max_degree = pre2num[i];
	unsigned limit_degree = max_degree / 2;
	for(TYPE_PREDICATE_ID i = 0; i < limitID_predicate; ++i)
		if (pre2num[i] > limit_degree)
			important_preID.push_back(i);
}

void
Database::load_important_obj2values()
{
	cout << "get important objID..." << endl;
	this->get_important_objID();

	this->build_CacheOfObj2values();
}
void
Database::load_important_sub2values()
{
	cout << "get important subID..." << endl;
	this->get_important_subID();

	this->build_CacheOfSub2values();
}

void 
Database::load_candidate_pre2values()
{
	cout << "get candidate preID..." << endl;
	this->get_candidate_preID();

	this->build_CacheOfPre2values();
}

void
Database::get_candidate_preID()
{
	
	






	unsigned now_total_size = 0;
	const unsigned max_total_size = 2000000000; 

	std::priority_queue <KEY_SIZE_VALUE, deque<KEY_SIZE_VALUE>, greater<KEY_SIZE_VALUE> > rubbish;
	while(!rubbish.empty()) rubbish.pop();
	while(!candidate_preID.empty()) candidate_preID.pop();
	for(TYPE_PREDICATE_ID i = 0; i < limitID_predicate; ++i)
	{
		unsigned _value = 0;
		unsigned _size;
		unsigned long _tmp_size;
		_tmp_size = this->kvstore->getPreListSize(i);
		if (_tmp_size > (1 << 31)) continue;
		_size = (unsigned)_tmp_size;
		if (!VList::isLongList(_size) || _size >= max_total_size) continue; 

		_value = pre2num[i];
		if (_value == 0) continue;

		if (_size + now_total_size < max_total_size)
		{
			candidate_preID.push(KEY_SIZE_VALUE(i, _size, _value));
			now_total_size += _size;
		}
		else
		{
			if (candidate_preID.empty()) continue;
			if (_value > candidate_preID.top().value)
			{
				while (now_total_size + _size >= max_total_size)
				{
					if (candidate_preID.top().value >= _value) break;
					rubbish.push(candidate_preID.top());
					now_total_size -= candidate_preID.top().size;
					candidate_preID.pop();
				}
				if (now_total_size + _size < max_total_size)
				{
					now_total_size += _size;
					candidate_preID.push(KEY_SIZE_VALUE(i, _size, _value));
				}
				while (!rubbish.empty())
				{
					if (now_total_size + rubbish.top().size < max_total_size)
					{
						now_total_size += rubbish.top().size;
						candidate_preID.push(rubbish.top());
					}
					rubbish.pop();
				}
			}
		}
	}
	cout << "finish getting candidate preID, the size is " << now_total_size << endl;
}

void
Database::build_CacheOfPre2values()
{
	cout << "now add cache of preID2values..." << endl;
	while (!candidate_preID.empty())
	{
		this->kvstore->AddIntoPreCache(candidate_preID.top().key);
		candidate_preID.pop();
	}
}

void
Database::build_CacheOfObj2values()
{
	cout << "now add cache of objID2values..." << endl;
	while (!important_objID.empty())
	{
		this->kvstore->AddIntoObjCache(important_objID.top().key);
		important_objID.pop();
	}
}

void
Database::build_CacheOfSub2values()
{
	cout << "now add cache of subID2values..." << endl;
	while (!important_subID.empty())
	{
		this->kvstore->AddIntoSubCache(important_subID.top().key);
		important_subID.pop();
	}
}

void
Database::get_important_subID()
{
	while(!important_subID.empty()) important_subID.pop();
	unsigned now_total_size = 0;
	const string invalid = "";
	const unsigned max_total_size = 2000000000; 
	std::priority_queue <KEY_SIZE_VALUE, deque<KEY_SIZE_VALUE>, greater<KEY_SIZE_VALUE> > rubbish;
	while(!rubbish.empty()) rubbish.pop();
	
	for(TYPE_ENTITY_LITERAL_ID i = 0; i < limitID_entity; ++i)
	{
		unsigned _value = 0;
		unsigned long _tmp_size = 0;
		if (this->kvstore->getEntityByID(i) == invalid) continue;	
		_tmp_size = this->kvstore->getSubListSize(i);
		if (_tmp_size >= (1 << 31)) continue;
		unsigned _size = (unsigned)_tmp_size;
		if (!VList::isLongList(_size) || _size >= max_total_size) continue; 

		for(unsigned j = 0; j < important_preID.size(); ++j)
		{
			_value += this->kvstore->getSubjectPredicateDegree(i, j);
		}
		if (_size + now_total_size < max_total_size)
		{
			important_subID.push(KEY_SIZE_VALUE(i, _size, _value));
			now_total_size += _size;
		}
		else
		{
			if (important_subID.empty()) continue;
			if (_value > important_subID.top().value)
			{
				while (now_total_size + _size >= max_total_size)
				{
					if (important_subID.top().value >= _value) break;
					rubbish.push(important_subID.top());
					now_total_size -= important_subID.top().size;
					important_subID.pop();
				}
				if (now_total_size + _size < max_total_size)
				{
					now_total_size += _size;
					important_subID.push(KEY_SIZE_VALUE(i, _size, _value));
				}
				while (!rubbish.empty())
				{
					if (now_total_size + rubbish.top().size < max_total_size)
					{
						now_total_size += rubbish.top().size;
						important_subID.push(rubbish.top());
					}
					rubbish.pop();
				}
			}
		}
	}
	cout << "finish getting important subID, the cache size is " << now_total_size << endl;
}

void
Database::get_important_objID()
{
	while(!important_objID.empty()) important_objID.pop();
	unsigned now_total_size = 0;
	const unsigned max_total_size = 2000000000; 
	const string invalid = "";
	std::priority_queue <KEY_SIZE_VALUE, deque<KEY_SIZE_VALUE>, greater<KEY_SIZE_VALUE> > rubbish;
	while(!rubbish.empty()) rubbish.pop();
	
	for(TYPE_ENTITY_LITERAL_ID i = 0; i < limitID_literal; ++i)
	{
		unsigned _value = 0;
		unsigned _size;
		string _tmp;
		if (i < limitID_entity) _tmp = this->kvstore->getEntityByID(i);
		else _tmp = this->kvstore->getLiteralByID(i);
		if (_tmp == invalid) continue;

		unsigned long _tmp_size = 0;
		_tmp_size = this->kvstore->getSubListSize(i);
		
		_tmp_size = this->kvstore->getObjListSize(i);
		if (_tmp_size >= (1 << 31)) continue;
		_size = (unsigned)_tmp_size;

		if (!VList::isLongList(_size) || _size >= max_total_size) continue; 
		
		for(unsigned j = 0; j < important_preID.size(); ++j)
		{
			_value += this->kvstore->getObjectPredicateDegree(i, j);
		}
		
		if (_size + now_total_size < max_total_size)
		{
			important_objID.push(KEY_SIZE_VALUE(i, _size, _value));
			now_total_size += _size;
		}
		else
		{
			if (important_objID.empty()) continue;
			if (_value > important_objID.top().value)
			{
				while (now_total_size + _size >= max_total_size)
				{
					if (important_objID.top().value >= _value) break;
					rubbish.push(important_objID.top());
					now_total_size -= important_objID.top().size;
					important_objID.pop();
				}
				if (now_total_size + _size < max_total_size)
				{
					now_total_size += _size;
					important_objID.push(KEY_SIZE_VALUE(i, _size, _value));
				}
				while (!rubbish.empty())
				{
					if (now_total_size + rubbish.top().size < max_total_size)
					{
						now_total_size += rubbish.top().size;
						important_objID.push(rubbish.top());
					}
					rubbish.pop();
				}
			}
		}
	}
	cout << endl;
	cout << "finish getting important objID, the cache size is " << now_total_size << endl;
}

void 
Database::load_entity2id(int _mode)
{
	this->kvstore->open_entity2id(_mode);
}

void 
Database::load_id2entity(int _mode)
{
	this->kvstore->open_id2entity(_mode);
}

void 
Database::load_literal2id(int _mode)
{
	this->kvstore->open_literal2id(_mode);
}

void 
Database::load_id2literal(int _mode)
{
	this->kvstore->open_id2literal(_mode);
}

void 
Database::load_predicate2id(int _mode)
{
	this->kvstore->open_predicate2id(_mode);
}

void 
Database::load_id2predicate(int _mode)
{
	this->kvstore->open_id2predicate(_mode);
}

void 
Database::load_sub2values(int _mode)
{
	this->kvstore->open_subID2values(_mode);
}

void 
Database::load_obj2values(int _mode)
{
	this->kvstore->open_objID2values(_mode);
}

void 
Database::load_pre2values(int _mode)
{
	this->kvstore->open_preID2values(_mode);
}











void 
Database::check()
{
cout<<"triple num: "<<this->triples_num<<endl;
cout<<"pre num: "<<this->pre_num<<endl;
cout<<"entity num: "<<this->entity_num<<endl;
cout<<"literal num: "<<this->literal_num<<endl;

string tstr;













 
 
 
 
 

	
 
 
 
 
 

 
 
 
 
 

 
 
 


	
	
	
	
	
	
	
	
			
	
	
	
			
	
	
	

















	



	



	string spq[6];
	spq[0] = "select ?x where { ?x <ub:name> <FullProfessor0> . }";
	spq[1] = "select distinct ?x where { ?x      <rdf:type>      <ub:GraduateStudent>. ?y      <rdf:type>      <ub:University>. ?z      <rdf:type>      <ub:Department>. ?x      <ub:memberOf>   ?z. ?z      <ub:subOrganizationOf>  ?y. ?x      <ub:undergraduateDegreeFrom>    ?y. }";
	spq[2] = "select distinct ?x where { ?x      <rdf:type>      <ub:Course>. ?x      <ub:name>       ?y. }";
	spq[3] = "select ?x where { ?x    <rdf:type>    <ub:UndergraduateStudent>. ?y    <ub:name> <Course1>. ?x    <ub:takesCourse>  ?y. ?z    <ub:teacherOf>    ?y. ?z    <ub:name> <FullProfessor1>. ?z    <ub:worksFor>    ?w. ?w    <ub:name>    <Department0>. }";
		spq[4] = "select distinct ?x where { ?x    <rdf:type>    <ub:UndergraduateStudent>. }";
	spq[5] = "select ?s ?o where { ?s ?p ?o . }";
	for(int i = 0; i < 6; ++i)
	{
		
		
		
		
	}
	
	
	
}

void 
Database::query_stringIndex(int id)
{
	string str;
	this->stringindex->randomAccess(id, &str, true);
	cout<<"thread: "<<id<<" "<<str<<endl;
}




bool
Database::unload()
{
	
	
	
	delete[] this->pre2num;
	this->pre2num = NULL;
	delete[] this->pre2sub;
	this->pre2sub = NULL;
	delete[] this->pre2obj;
	this->pre2obj = NULL;
	
	delete this->entity_buffer;
	this->entity_buffer = NULL;
	
	delete this->literal_buffer;
	this->literal_buffer = NULL;

	
	
	
	
	delete this->kvstore;
	this->kvstore = NULL;
	
	delete this->stringindex;
	this->stringindex = NULL;

	this->saveDBInfoFile();
	this->writeIDinfo();
	this->initIDinfo();

	this->if_loaded = false;
	this->clear_update_log();

	





	return true;
}



bool Database::save()
{
	
	this->kvstore->flush();
	this->saveDBInfoFile();
	this->saveIDinfo();

	this->stringindex->flush();
	this->clear_update_log();

	

	return true;
}

void Database::clear() 
{
	delete[] this->pre2num;
	this->pre2num = NULL;
	delete[] this->pre2sub;
	this->pre2sub = NULL;
	delete[] this->pre2obj;
	this->pre2obj = NULL;
	delete this->entity_buffer;
	this->entity_buffer = NULL;
	delete this->literal_buffer;
	this->literal_buffer = NULL;

	
	
	delete this->kvstore;
	this->kvstore = NULL;
	delete this->stringindex;
	this->stringindex = NULL;
}

string
Database::getName()
{
	return this->name;
}

TYPE_TRIPLE_NUM 
Database::getTripleNum()
{
	return this->triples_num;
}

TYPE_ENTITY_LITERAL_ID 
Database::getEntityNum()
{
	return this->entity_num;
}

TYPE_ENTITY_LITERAL_ID 
Database::getLiteralNum()
{
	return this->literal_num;
}

TYPE_ENTITY_LITERAL_ID 
Database::getSubNum()
{
	return this->sub_num;
}

TYPE_PREDICATE_ID 
Database::getPreNum()
{
	return this->pre_num;
}







KVstore*
Database::getKVstore()
{
	return this->kvstore;
}

StringIndex*
Database::getStringIndex()
{
	return this->stringindex;
}

QueryCache*
Database::getQueryCache()
{
	return this->query_cache;
}

TYPE_TRIPLE_NUM*
Database::getpre2num()
{
	return this->pre2num;
}

TYPE_TRIPLE_NUM*
Database::getpre2sub()
{
	return this->pre2sub;
}

TYPE_TRIPLE_NUM*
Database::getpre2obj()
{
	return this->pre2obj;
}

TYPE_ENTITY_LITERAL_ID&
Database::getlimitID_literal()
{
	return this->limitID_literal;
}

TYPE_ENTITY_LITERAL_ID&
Database::getlimitID_entity()
{
	return this->limitID_entity;
}

TYPE_PREDICATE_ID&
Database::getlimitID_predicate()
{
	return this->limitID_predicate;
}

mutex&
Database::get_query_parse_lock()
{
	return this->query_parse_lock;
}

void
Database::setEmbeddingPrecomputeTime(long time_ms)
{
	this->embedding_precompute_time_ms = time_ms;
}

int
Database::query(const string _query, ResultSet& _result_set, FILE* _fp,
                bool update_flag, bool export_flag, shared_ptr<Transaction> txn,TopKStrategy top_k_method)
{
	string dictionary_store_path = this->store_path + "/dictionary.dc"; 	

	this->stringindex->SetTrie(this->kvstore->getTrie());
	GeneralEvaluation general_evaluation(this->kvstore, this->statistics, this->stringindex, this->query_cache, \
		this->pre2num, this->pre2sub, this->pre2obj, this->limitID_predicate, this->limitID_literal, \
		this->limitID_entity, this->csr,this->hop_index.get(), txn);
	
	
	long tv_begin = Util::get_cur_time();

	this->query_parse_lock.lock();
	bool parse_ret = general_evaluation.parseQuery(_query);
	this->query_parse_lock.unlock();
	if (!parse_ret)
		return -101;
	long tv_parse = Util::get_cur_time();
	cout << "after Parsing, used " << (tv_parse - tv_begin) << "ms." << endl;
	
	
	

	
	
	int success_num = -100;  
	bool need_output_answer = false;
	
	
	if (general_evaluation.getQueryTree().getUpdateType() == QueryTree::Not_Update)
	{
		
		
		
		
		if(txn == nullptr && pthread_rwlock_tryrdlock(&(this->update_lock)) != 0)
		{
			return -101;
		}
		if(txn == nullptr)
			cout<<"read priviledge of update lock acquired"<<endl;

		
		
		
		
	
	
		if(export_flag)
		{
			general_evaluation.fp = _fp;
			general_evaluation.export_flag = export_flag;
		}

		long t1 = Util::get_cur_time();
		bool query_ret = general_evaluation.doQuery(top_k_method);
		long t2 = Util::get_cur_time();
		cout << "GeneralEvaluation::doQuery used " << (t2 - t1) << "ms." << endl;

		if(!query_ret)
		{
			success_num = -101;
		}
	

		long tv_bfget = Util::get_cur_time();
		
		this->getFinalResult_lock.lock();
		










		general_evaluation.getFinalResult(_result_set);
		this->getFinalResult_lock.unlock();
		long tv_afget = Util::get_cur_time();
		cout << "during getFinalResult, used " << (tv_afget - tv_bfget) << "ms." << endl;

		if(_fp != NULL)
			need_output_answer = true;
			

		
		if(txn == nullptr)
			pthread_rwlock_unlock(&(this->update_lock));
	}
	
	else
	{
		if(update_flag == 0)
		{
			
			string exception_msg = "no update prvilege, update query failed.";
			cout << exception_msg << endl;
			throw exception_msg;
		}
#ifdef ONLY_READ
		cout<<"this database is only read";
		
		return -101;
#endif
		if(txn == nullptr && pthread_rwlock_trywrlock(&(this->update_lock)) != 0)
		{
			cout<<"unable to write lock"<<endl;
			return -101;
		}
		if(txn == nullptr)
			cout<<"write priviledge of update lock acquired"<<endl;

		success_num = 0;
		TripleWithObjType *update_triple = NULL;
		TYPE_TRIPLE_NUM update_triple_num = 0;
		










		if (general_evaluation.getQueryTree().getUpdateType() == QueryTree::Insert_Data || general_evaluation.getQueryTree().getUpdateType() == QueryTree::Delete_Data)
		{
			QueryTree::GroupPattern &update_pattern = general_evaluation.getQueryTree().getUpdateType() == QueryTree::Insert_Data ?
				general_evaluation.getQueryTree().getInsertPatterns() : general_evaluation.getQueryTree().getDeletePatterns();

			update_triple_num = update_pattern.sub_group_pattern.size();
			update_triple = new TripleWithObjType[update_triple_num];

			for (TYPE_TRIPLE_NUM i = 0; i < update_triple_num; i++)
				if (update_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
				{
					TripleWithObjType::ObjectType object_type = TripleWithObjType::None;
					if (update_pattern.sub_group_pattern[i].pattern.object.value[0] == '<')
						object_type = TripleWithObjType::Entity;
					else
						object_type = TripleWithObjType::Literal;

					update_triple[i] = TripleWithObjType(update_pattern.sub_group_pattern[i].pattern.subject.value,
														 update_pattern.sub_group_pattern[i].pattern.predicate.value,
														 update_pattern.sub_group_pattern[i].pattern.object.value, object_type);

					
					
				}
				else 
				{
					if(txn == nullptr)
					{
						pthread_rwlock_unlock(&(this->update_lock));
						throw "Database::query failed";
					}
				}

			if (general_evaluation.getQueryTree().getUpdateType() == QueryTree::Insert_Data)
			{
				success_num = insert(update_triple, update_triple_num, false, txn);
			}
			else if (general_evaluation.getQueryTree().getUpdateType() == QueryTree::Delete_Data)
			{
				success_num = remove(update_triple, update_triple_num, false, txn);
			}
		}
		else if (general_evaluation.getQueryTree().getUpdateType() == QueryTree::Delete_Where || general_evaluation.getQueryTree().getUpdateType() == QueryTree::Insert_Clause ||
			general_evaluation.getQueryTree().getUpdateType() == QueryTree::Delete_Clause || general_evaluation.getQueryTree().getUpdateType() == QueryTree::Modify_Clause)
		{
			general_evaluation.getQueryTree().setProjectionAsterisk();
			general_evaluation.doQuery();

			if (general_evaluation.getQueryTree().getUpdateType() == QueryTree::Delete_Where || general_evaluation.getQueryTree().getUpdateType() == QueryTree::Delete_Clause || general_evaluation.getQueryTree().getUpdateType() == QueryTree::Modify_Clause)
			{
				general_evaluation.prepareUpdateTriple(general_evaluation.getQueryTree().getDeletePatterns(), update_triple, update_triple_num);
				
				
					
				
				success_num = remove(update_triple, update_triple_num, false, txn);

			}
			if (general_evaluation.getQueryTree().getUpdateType() == QueryTree::Insert_Clause || general_evaluation.getQueryTree().getUpdateType() == QueryTree::Modify_Clause)
			{
				general_evaluation.prepareUpdateTriple(general_evaluation.getQueryTree().getInsertPatterns(), update_triple, update_triple_num);
				
				
					
				
				success_num = insert(update_triple, update_triple_num, false, txn);
			}
		}

		general_evaluation.releaseResult();
		delete[] update_triple;

		
		if(success_num > 0)
		{
			this->query_cache->clear();
			cout<<"QueryCache cleared"<<endl;
		}
		if(txn == nullptr)
			pthread_rwlock_unlock(&(this->update_lock));
	}

	long tv_final = Util::get_cur_time();
	long embedding_precompute_time = this->embedding_precompute_time_ms;
	this->embedding_precompute_time_ms = 0;
	cout << "Query time used (minus parsing): " << (tv_final - tv_parse + embedding_precompute_time) << "ms." << endl;
	cout << "Total time used: " << (tv_final - tv_begin + embedding_precompute_time) << "ms." << endl;
	
	if(!export_flag)
	{
		if (need_output_answer)
		{
			long long ans_num = max((long long)_result_set.ansNum - _result_set.output_offset, 0LL);
			if (_result_set.output_limit != -1)
				ans_num = min(ans_num, (long long)_result_set.output_limit);
			cout << "There has answer: " << ans_num << endl;
			cout << "final result is : " << endl;
			_result_set.output(_fp);
			fprintf(_fp, "\n");
			fflush(_fp);       
		}
	}

#ifdef DEBUG
	cout<<"query success_num: "<<success_num<<endl;
#endif

	
	return success_num;
}










bool
Database::build(const string& _rdf_file)
{
	
	
	
	
	

	
	this->resetIDinfo();

	string ret = Util::getExactPath(_rdf_file.c_str());
	long tv_build_begin = Util::get_cur_time();

	
	Util::create_dir(this->store_path);

	string kv_store_path = store_path + "/kv_store";
	Util::create_dir(kv_store_path);

	
	

	string stringindex_store_path = store_path + "/stringindex_store";
	Util::create_dir(stringindex_store_path);

	string update_log_path = this->store_path + '/' + this->update_log;
	Util::create_file(update_log_path);
	string update_log_since_backup = this->store_path + '/' + this->update_log_since_backup;
	Util::create_file(update_log_since_backup);

	cout << "begin encode RDF from : " << ret << " ..." << endl;

	
	
	
	
	
	

	
	
	if (!this->encodeRDF_new(ret))	
	{
		return false;
	}
	cout << "finish encode." << endl;

	

	
	delete this->kvstore;
	this->kvstore = NULL;
	
	
	
	

	
	
	

	
	
	
	
	
	
	

	long tv_build_end = Util::get_cur_time();

	
	cout << "after build, used " << (tv_build_end - tv_build_begin) << "ms." << endl;
	cout << "finish build VS-Tree." << endl;

	cout << "finish sub2id pre2id obj2id" << endl;
	cout << "tripleNum is " << this->triples_num << endl;
	cout << "entityNum is " << this->entity_num << endl;
	cout << "preNum is " << this->pre_num << endl;
	cout << "literalNum is " << this->literal_num << endl;

	
	
	
	
	

	
	
	
	

	return true;
}

bool
Database::BuildHopIndex()
{
  this->hop_index = unique_ptr<HopIndex>(new HopIndex(this->entity_num,this->limitID_predicate));
  this->hop_index->LoadPredicates(this->kvstore,this->getStorePath() + "/HopIndexPredicates.txt");
  this->hop_index->Build(this->kvstore);
  this->hop_index->Save(this->getStorePath() + "/HopIndex");
  return true;
}

bool
Database::LoadHopIndex(bool load_hop_index)
{
  this->hop_index = unique_ptr<HopIndex>(new HopIndex(this->entity_num,this->limitID_predicate));
  if(load_hop_index)
    this->hop_index->Load(this->getStorePath() + "/HopIndex");
  return true;
}


string
Database::getSixTuplesFile()
{
	return this->getStorePath() + "/" + this->six_tuples_file;
}









string
Database::getDBInfoFile()
{
	return this->getStorePath() + "/" + this->db_info_file;
}

string
Database::getIDTuplesFile()
{
	return this->getStorePath() + "/" + this->id_tuples_file;
}

bool
Database::saveDBInfoFile()
{
	FILE* filePtr = fopen(this->getDBInfoFile().c_str(), "wb");

	if (filePtr == NULL)
	{
		cout << "error, can not create db info file in Database::saveDBInfoFile" << endl;
		return false;
	}

	fseek(filePtr, 0, SEEK_SET);

	fwrite(&this->triples_num, sizeof(TYPE_TRIPLE_NUM), 1, filePtr);
	fwrite(&this->entity_num, sizeof(TYPE_ENTITY_LITERAL_ID), 1, filePtr);
	fwrite(&this->sub_num, sizeof(TYPE_ENTITY_LITERAL_ID), 1, filePtr);
	fwrite(&this->pre_num, sizeof(TYPE_PREDICATE_ID), 1, filePtr);
	fwrite(&this->literal_num, sizeof(TYPE_ENTITY_LITERAL_ID), 1, filePtr);
	fwrite(&this->encode_mode, sizeof(int), 1, filePtr);

	Util::Csync(filePtr);
	fclose(filePtr);

	
	
	
	

	return true;
}

bool
Database::loadDBInfoFile()
{
	FILE* filePtr = fopen(this->getDBInfoFile().c_str(), "rb");

	if (filePtr == NULL)
	{
		cout << "error, can not open db info file in Database::loadDBInfoFile" << endl;
		return false;
	}

	fseek(filePtr, 0, SEEK_SET);

	fread(&this->triples_num, sizeof(TYPE_TRIPLE_NUM), 1, filePtr);
	fread(&this->entity_num, sizeof(TYPE_ENTITY_LITERAL_ID), 1, filePtr);
	fread(&this->sub_num, sizeof(TYPE_ENTITY_LITERAL_ID), 1, filePtr);
	fread(&this->pre_num, sizeof(TYPE_PREDICATE_ID), 1, filePtr);
	fread(&this->literal_num, sizeof(TYPE_ENTITY_LITERAL_ID), 1, filePtr);
	fread(&this->encode_mode, sizeof(int), 1, filePtr);
	fclose(filePtr);

	
	
	
	

	return true;
}

string
Database::getStorePath()
{
	return this->store_path;
}




































































































































bool
Database::exist_triple(TYPE_ENTITY_LITERAL_ID _sub_id, TYPE_PREDICATE_ID _pre_id, TYPE_ENTITY_LITERAL_ID _obj_id, shared_ptr<Transaction> txn)
{
	unsigned* _objidlist = NULL;
	unsigned _list_len = 0;
	
	(this->kvstore)->getobjIDlistBysubIDpreID(_sub_id, _pre_id, _objidlist, _list_len, true, txn);

	bool is_exist = false;
	
	
	
	
	
	
	
	
	if (Util::bsearch_int_uporder(_obj_id, _objidlist, _list_len) != INVALID)
	
	{
		is_exist = true;
	}
	delete[] _objidlist;

	return is_exist;
}

bool Database::exist_triple(const TripleWithObjType& _triple, shared_ptr<Transaction> txn) {
	int sub_id = this->kvstore->getIDByEntity(_triple.getSubject());
	if (sub_id == -1) {
		return false;
	}

	int pre_id = this->kvstore->getIDByPredicate(_triple.getPredicate());
	if (pre_id == -1) {
		return false;
	}

	int obj_id = -1;
	if (_triple.isObjEntity()) {
		obj_id = this->kvstore->getIDByEntity(_triple.getObject());
	}
	else if (_triple.isObjLiteral()) {
		obj_id = this->kvstore->getIDByLiteral(_triple.getObject());
	}
	if (obj_id == -1) {
		return false;
	}

	return exist_triple(sub_id, pre_id, obj_id, txn);
}



bool
Database::encodeRDF_new(const string _rdf_file)
{
#ifdef DEBUG
	
	Util::logging("In encodeRDF_new");
	
#endif

	
	ID_TUPLE* _p_id_tuples = NULL;
	TYPE_TRIPLE_NUM _id_tuples_max = 0;

	long t1 = Util::get_cur_time();

	
	
	
	
	

	
	if (!this->sub2id_pre2id_obj2id_RDFintoSignature(_rdf_file))
	{
		return false;
	}

	
	
	
	
	
	
	
	

	long t2 = Util::get_cur_time();
	cout << "after encode, used " << (t2 - t1) << "ms." << endl;

	
	this->stringindex->setNum(StringIndexFile::Entity, this->entity_num);
	this->stringindex->setNum(StringIndexFile::Literal, this->literal_num);
	this->stringindex->setNum(StringIndexFile::Predicate, this->pre_num);
	this->stringindex->save(*this->kvstore);
	
	
	

	long t3 = Util::get_cur_time();
	cout << "after stringindex, used " << (t3 - t2) << "ms." << endl;

	

	
	this->kvstore->close_entity2id();
	this->kvstore->close_id2entity();
	this->kvstore->close_literal2id();
	this->kvstore->close_id2literal();
	this->kvstore->close_predicate2id();
    this->kvstore->close_id2predicate();

	long t4 = Util::get_cur_time();
	cout << "id2string and string2id closed, used " << (t4 - t3) << "ms." << endl;

	
	
	
	this->readIDTuples(_p_id_tuples);

	
	
	

	long t5 = Util::get_cur_time();
	cout << "id tuples read, used " << (t5 - t4) << "ms." << endl;

	

	
	this->build_s2xx(_p_id_tuples);

	long t6 = Util::get_cur_time();
	cout << "after s2xx, used " << (t6 - t5) << "ms." << endl;

	
	this->build_o2xx(_p_id_tuples);

	long t7 = Util::get_cur_time();
	cout << "after o2xx, used " << (t7 - t6) << "ms." << endl;

	
	this->build_p2xx(_p_id_tuples);

	long t8 = Util::get_cur_time();
	cout << "after p2xx, used " << (t8 - t7) << "ms." << endl;

	
	delete[] _p_id_tuples;

	
	
		
	
	

	

	bool flag = this->saveDBInfoFile();
	if (!flag)
	{
		return false;
	}

	long t9 = Util::get_cur_time();
	cout << "db info saved, used " << (t9 - t8) << "ms." << endl;

	

	
  
































  return true;
}

void
Database::readIDTuples(ID_TUPLE*& _p_id_tuples)
{
  _p_id_tuples = NULL;
  string fname = this->getIDTuplesFile();
  FILE* fp = fopen(fname.c_str(), "rb");
  if(fp == NULL)
  {
      cout<<"error in Database::readIDTuples() -- unable to open file "<<fname<<endl;
      return;
  }

  
  
  
  
  _p_id_tuples = new ID_TUPLE[this->triples_num];
  fread(_p_id_tuples, sizeof(ID_TUPLE), this->triples_num, fp);

  fclose(fp);
  
  Util::empty_file(fname.c_str());

  
}

void
Database::build_s2xx(ID_TUPLE* _p_id_tuples)
{
  
  
#ifndef PARALLEL_SORT
  sort(_p_id_tuples, _p_id_tuples + this->triples_num, Util::spo_cmp_idtuple);
#else
  omp_set_num_threads(thread_num);
  __gnu_parallel::sort(_p_id_tuples, _p_id_tuples + this->triples_num, Util::spo_cmp_idtuple);
#endif
  

  
  TYPE_TRIPLE_NUM j = 1;
  for(TYPE_TRIPLE_NUM i = 1; i < this->triples_num; ++i)
  {
      if(!Util::equal(_p_id_tuples[i], _p_id_tuples[i-1]))
      {
          _p_id_tuples[j] = _p_id_tuples[i];
          ++j;
      }
  }
  this->triples_num = j;

  this->kvstore->build_subID2values(_p_id_tuples, this->triples_num, this->entity_num);

  
  
  
  
  
      
      
  

  
  
  
  
  
      
      
      
  

  
  

  
  
  
  
      
      
      
      
      
      
      
      
          
          
          

              
              
                  
              

              
              
              
              
              
              
              
          
          
          
          
          
          
      
      
      
          
      
  

  
  
  
  
      
      
      
      
      
      
  

  
}

void
Database::build_o2xx(ID_TUPLE* _p_id_tuples)
{
#ifndef PARALLEL_SORT
  sort(_p_id_tuples, _p_id_tuples + this->triples_num, Util::ops_cmp_idtuple);
#else
  omp_set_num_threads(thread_num);
  __gnu_parallel::sort(_p_id_tuples, _p_id_tuples + this->triples_num, Util::ops_cmp_idtuple);
#endif
  
  this->kvstore->build_objID2values(_p_id_tuples, this->triples_num, this->entity_num, this->literal_num);

  
  
  
  
  
  
      
      
  

  
  
  
  
  

  
  
  
  
      
      
      
      
      
      


      
      
          
      

      
      
          
          
          
              
              
              
              

              
              
              
              
                  
              

              
              


              
              
                  
              


                  
              
              
              
              
              
          


          


          
          
          
          
          
          
          
      
      
      
          
          
          
      
  
  

  
  
  
  
      

      
      
      
      
      
      
      
      
      
      
      
  

  
}

void
Database::build_p2xx(ID_TUPLE* _p_id_tuples)
{
#ifndef PARALLEL_SORT
  sort(_p_id_tuples, _p_id_tuples + this->triples_num, Util::pso_cmp_idtuple);
#else
  omp_set_num_threads(thread_num);
  __gnu_parallel::sort(_p_id_tuples, _p_id_tuples + this->triples_num, Util::pso_cmp_idtuple);
#endif
  
  this->kvstore->build_preID2values(_p_id_tuples, this->triples_num, this->pre_num);
}






bool
Database::sub2id_pre2id_obj2id_RDFintoSignature(const string _rdf_file)
{
  
  
  
  
  
  

  string fname = this->getIDTuplesFile();
  FILE* fp = fopen(fname.c_str(), "wb");
  if(fp == NULL)
  {
      cout<<"error in Database::sub2id_pre2id_obj2id() -- unable to open file to write "<<fname<<endl;
      return false;
  }
  ID_TUPLE tmp_id_tuple;
  
  
  

  TYPE_TRIPLE_NUM _id_tuples_size;
  {
      
      
      
      
      this->sub_num = 0;
      this->pre_num = 0;
      this->entity_num = 0;
      this->literal_num = 0;
      this->triples_num = 0;
      (this->kvstore)->open_entity2id(KVstore::CREATE_MODE);
      (this->kvstore)->open_id2entity(KVstore::CREATE_MODE);
      (this->kvstore)->open_predicate2id(KVstore::CREATE_MODE);
      (this->kvstore)->open_id2predicate(KVstore::CREATE_MODE);
      (this->kvstore)->open_literal2id(KVstore::CREATE_MODE);
      (this->kvstore)->open_id2literal(KVstore::CREATE_MODE);
      (this->kvstore)->load_trie(KVstore::CREATE_MODE);
  }

  
  cout << "finish initial sub2id_pre2id_obj2id" << endl;

  

  ifstream _fin(_rdf_file.c_str());
  if (!_fin)
  {
      cout << "sub2id&pre2id&obj2id: Fail to open : " << _rdf_file << endl;
      
      return false;
  }

  string _six_tuples_file = this->getSixTuplesFile();
  ofstream _six_tuples_fout(_six_tuples_file.c_str());
  if (!_six_tuples_fout)
  {
      cout << "sub2id&pre2id&obj2id: Fail to open: " << _six_tuples_file << endl;
      
      return false;
  }

  TripleWithObjType* triple_array = new TripleWithObjType[RDFParser::TRIPLE_NUM_PER_GROUP];

  
  
  
  
  
  
  
  
      
      
  
  

  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  

  RDFParser _parser(_fin);
  

  while (true)
  {
      int parse_triple_num = 0;

      _parser.parseFile(triple_array, parse_triple_num);

      {
          stringstream _ss;
          _ss << "finish rdfparser" << this->triples_num << endl;
          
          cout << _ss.str() << endl;
      }
      cout << "after info in sub2id_" << endl;

      if (parse_triple_num == 0)
      {
          break;
      }

      
      for (int i = 0; i < parse_triple_num; i++)
      {
          
          
          
          this->triples_num++;

          
          
          
              
              
              
              
              
              
          

          
          
          

          
          
          string _sub = triple_array[i].getSubject();
          TYPE_ENTITY_LITERAL_ID _sub_id = (this->kvstore)->getIDByEntity(_sub);
          if (_sub_id == INVALID_ENTITY_LITERAL_ID)
          
          {
              
              _sub_id = this->allocEntityID();
              this->sub_num++;
              
              (this->kvstore)->setIDByEntity(_sub, _sub_id);
              (this->kvstore)->setEntityByID(_sub_id, _sub);
          }
          
          string _pre = triple_array[i].getPredicate();
          TYPE_PREDICATE_ID _pre_id = (this->kvstore)->getIDByPredicate(_pre);
          if (_pre_id == INVALID_PREDICATE_ID)
          
          {
              
              _pre_id = this->allocPredicateID();
              
              (this->kvstore)->setIDByPredicate(_pre, _pre_id);
              (this->kvstore)->setPredicateByID(_pre_id, _pre);
          }

          
          string _obj = triple_array[i].getObject();
          
          TYPE_ENTITY_LITERAL_ID _obj_id = INVALID_ENTITY_LITERAL_ID;
          
          if (triple_array[i].isObjEntity())
          {
              _obj_id = (this->kvstore)->getIDByEntity(_obj);
              if (_obj_id == INVALID_ENTITY_LITERAL_ID)
              
              {
                  
                  _obj_id = this->allocEntityID();
                  
                  (this->kvstore)->setIDByEntity(_obj, _obj_id);
                  (this->kvstore)->setEntityByID(_obj_id, _obj);
              }
          }
          
          if (triple_array[i].isObjLiteral())
          {
              _obj_id = (this->kvstore)->getIDByLiteral(_obj);
              if (_obj_id == INVALID_ENTITY_LITERAL_ID)
              
              {
                  
                  _obj_id = this->allocLiteralID();
                  
                  (this->kvstore)->setIDByLiteral(_obj, _obj_id);
                  (this->kvstore)->setLiteralByID(_obj_id, _obj);
                  
                  
                  
                  
                  
                  
                  
                  
              }
          }

          
          
          

          
          
          
          
          
          
          tmp_id_tuple.subid = _sub_id;
          tmp_id_tuple.preid = _pre_id;
          tmp_id_tuple.objid = _obj_id;
          fwrite(&tmp_id_tuple, sizeof(ID_TUPLE), 1, fp);
          
          
          

#ifdef DEBUG_PRECISE
          
              
                  
                  
                  
                  
                  
#endif

          
          
          
          
          
          
          
          
          
          
          

          
          
          
              
              
              
              
              
              
              

              
              
              
                  
                  
              

              
          

          
              
              
              
              
          

          
          
              
              
              
              
              
              
              
              
              
              
          
      }
  }
  this->kvstore->set_if_single_thread(false);
  

  delete[] triple_array;
  triple_array = NULL;
  _fin.close();
  _six_tuples_fout.close();
  fclose(fp);


      
      
          
      
      

  
      
      
      
      
      
      
      
      
  

  return true;
}

bool
Database::insertTriple(const TripleWithObjType& _triple, vector<unsigned>* _vertices, vector<unsigned>* _predicates, shared_ptr<Transaction> txn)
{
  
  
  
  

  
  
  
  
  

  long tv_kv_store_begin = Util::get_cur_time();

  TYPE_ENTITY_LITERAL_ID _sub_id = (this->kvstore)->getIDByEntity(_triple.subject);
  if(txn != nullptr)
      cout << "UpdateNode in Transaction...................................................." << endl;
  bool _is_new_sub = false;
  
  if (_sub_id == INVALID_ENTITY_LITERAL_ID)
  
  {
      _is_new_sub = true;
      
      _sub_id = this->allocEntityID();
      
      this->sub_num++;
      (this->kvstore)->setIDByEntity(_triple.subject, _sub_id);
      (this->kvstore)->setEntityByID(_sub_id, _triple.subject);

      
      
      
          
      

      if (_vertices != NULL)
          _vertices->push_back(_sub_id);
  }

  TYPE_PREDICATE_ID _pre_id = (this->kvstore)->getIDByPredicate(_triple.predicate);
  bool _is_new_pre = false;
  
  if (_pre_id == INVALID_PREDICATE_ID)
  
  {
      _is_new_pre = true;
      
      _pre_id = this->allocPredicateID();
      (this->kvstore)->setIDByPredicate(_triple.predicate, _pre_id);
      (this->kvstore)->setPredicateByID(_pre_id, _triple.predicate);

      if (_predicates != NULL)
          _predicates->push_back(_pre_id);
  }

  
  TYPE_ENTITY_LITERAL_ID _obj_id = INVALID_ENTITY_LITERAL_ID;
  
  bool _is_new_obj = false;
  bool is_obj_entity = _triple.isObjEntity();
  if (is_obj_entity)
  {
      _obj_id = (this->kvstore)->getIDByEntity(_triple.object);

      
      if (_obj_id == INVALID_ENTITY_LITERAL_ID)
      {
          _is_new_obj = true;
          
          _obj_id = this->allocEntityID();
          (this->kvstore)->setIDByEntity(_triple.object, _obj_id);
          (this->kvstore)->setEntityByID(_obj_id, _triple.object);

          
          
          
              
          

          if (_vertices != NULL)
              _vertices->push_back(_obj_id);
      }
  }
  else
  {
      _obj_id = (this->kvstore)->getIDByLiteral(_triple.object);
      

      
      if (_obj_id == INVALID_ENTITY_LITERAL_ID)
      {
          _is_new_obj = true;
          
          _obj_id = this->allocLiteralID();
          
          (this->kvstore)->setIDByLiteral(_triple.object, _obj_id);
          (this->kvstore)->setLiteralByID(_obj_id, _triple.object);
          
          

          
          
          
          
              
          

          if (_vertices != NULL)
              _vertices->push_back(_obj_id);
      }
  }


  if(txn != nullptr && (this->kvstore)->getExclusiveLocks(_sub_id, _pre_id, _obj_id, txn) == false)
  {
      
      
      txn->SetState(TransactionState::ABORTED);
      cout << "getExclusiveLocks failed, Abort" << endl;
      return false;
  }

  
  bool _triple_exist = false;
  if (!_is_new_sub && !_is_new_pre && !_is_new_obj)
  {
      _triple_exist = this->exist_triple(_sub_id, _pre_id, _obj_id, txn);
  }

  
  
  
  
  
  
  

  if (_triple_exist)
  {
      
      if(txn != nullptr){
          bool ret  = (this->kvstore)->releaseExclusiveLocks(_sub_id, _pre_id, _obj_id, txn);
          if(ret == false)
          {
              cerr << "...........................releaseExclusiveLocks failed!" << endl;
          }
      }
      cout << "this triple already exist" << endl;
      return false;
  }
  else
  {
      this->triples_num++;
  }
  

  
  bool ret = (this->kvstore)->updateTupleslist_insert(_sub_id, _pre_id, _obj_id, txn);
  if(txn)
  {
      cout << "WriteSetInsert......." << endl;
      if(ret)
          txn->WriteSetInsert(IDTriple(_sub_id, _pre_id, _obj_id));
      else{
          cout << "insert failed" << endl;
          txn->SetState(TransactionState::ABORTED);
          (this->kvstore)->releaseExclusiveLocks(_sub_id, _pre_id, _obj_id, txn);
      }
  }
  
  
  
  
  
  
  
  
  
  
  
  
  
  

  return true;
  
}

bool
Database::removeTriple(const TripleWithObjType& _triple, vector<unsigned>* _vertices, vector<unsigned>* _predicates, shared_ptr<Transaction> txn)
{
  long tv_kv_store_begin = Util::get_cur_time();

  TYPE_ENTITY_LITERAL_ID _sub_id = (this->kvstore)->getIDByEntity(_triple.subject);
  TYPE_PREDICATE_ID _pre_id = (this->kvstore)->getIDByPredicate(_triple.predicate);
  TYPE_ENTITY_LITERAL_ID _obj_id = INVALID_ENTITY_LITERAL_ID;
  if(_triple.isObjEntity())
  {
      _obj_id = (this->kvstore)->getIDByEntity(_triple.object);
  }
  else
  {
      _obj_id = (this->kvstore)->getIDByLiteral(_triple.object);
  }

  
  
  
      
  

  
  if (_sub_id == INVALID_ENTITY_LITERAL_ID || _pre_id == INVALID_PREDICATE_ID || _obj_id == INVALID_ENTITY_LITERAL_ID)
  {
      return false;
  }
  if(txn != nullptr && (this->kvstore)->getExclusiveLocks(_sub_id, _pre_id, _obj_id, txn) == false)
  {
      
      
      cout << "getExclusiveLocks...................... failed" << endl;
      txn->SetState(TransactionState::ABORTED);
      return false;
  }

  bool _exist_triple = this->exist_triple(_sub_id, _pre_id, _obj_id, txn);
  if (!_exist_triple)
  {
      
      cout << "triple is not exsited! " << endl;
      if(txn != nullptr)
          (this->kvstore)->releaseExclusiveLocks(_sub_id, _pre_id, _obj_id, txn);
      return false;
  }
  else
  {
      this->triples_num--;
  }

  

  
  
  bool ret = (this->kvstore)->updateTupleslist_remove(_sub_id, _pre_id, _obj_id, txn);
  if(txn)
  {
      if(ret)
          txn->WriteSetInsert(IDTriple(_sub_id, _pre_id, _obj_id));
      else{
          cout << " updateTupleslist_remove failed ..............................................." << endl;
          txn->SetState(TransactionState::ABORTED);
          (this->kvstore)->releaseExclusiveLocks(_sub_id, _pre_id, _obj_id, txn);
      }
  }
  

  long tv_kv_store_end = Util::get_cur_time();
  if(txn == nullptr)
  {
      int sub_degree = (this->kvstore)->getEntityDegree(_sub_id);
      
      if (sub_degree == 0)
      {
          
          
          this->kvstore->subEntityByID(_sub_id);
          this->kvstore->subIDByEntity(_triple.subject);
          this->freeEntityID(_sub_id);
          this->sub_num--;
          
          
          
              
          
          if (_vertices != NULL)
              _vertices->push_back(_sub_id);
      }
      

      bool is_obj_entity = _triple.isObjEntity();
      int obj_degree;
      if (is_obj_entity)
      {
          obj_degree = this->kvstore->getEntityDegree(_obj_id);
          if (obj_degree == 0)
          {
              
              
              this->kvstore->subEntityByID(_obj_id);
              this->kvstore->subIDByEntity(_triple.object);
              this->freeEntityID(_obj_id);
              
              
              
                  
              
              if (_vertices != NULL)
                  _vertices->push_back(_obj_id);
          }
      }
      else
      {
          obj_degree = this->kvstore->getLiteralDegree(_obj_id);
          if (obj_degree == 0)
          {
              this->kvstore->subLiteralByID(_obj_id);
              
              this->kvstore->subIDByLiteral(_triple.object);
              this->freeLiteralID(_obj_id);
              
              
              
              
                  
              
              if (_vertices != NULL)
                  _vertices->push_back(_obj_id);
          }
      }
      

      int pre_degree = this->kvstore->getPredicateDegree(_pre_id);
      if (pre_degree == 0)
      {
          this->kvstore->subPredicateByID(_pre_id);
          this->kvstore->subIDByPredicate(_triple.predicate);
          this->freePredicateID(_pre_id);
          if (_predicates != NULL)
              _predicates->push_back(_pre_id);
      }
      
  }

  return true;
}

bool
Database::insert(std::string _rdf_file, bool _is_restore, shared_ptr<Transaction> txn )
{
  bool flag = _is_restore || this->load();
  
  if (!flag)
  {
      return false;
  }
  cout << "finish loading" << endl;

  long tv_load = Util::get_cur_time();

  TYPE_TRIPLE_NUM success_num = 0;

  ifstream _fin(_rdf_file.c_str());
  if (!_fin)
  {
      cout << "fail to open: " << _rdf_file << " in insert_test" << endl;
      
      return false;
  }

  
  
  
  
  TripleWithObjType* triple_array = new TripleWithObjType[RDFParser::TRIPLE_NUM_PER_GROUP];
  
  RDFParser _parser(_fin);

  TYPE_TRIPLE_NUM triple_num = 0;
#ifdef DEBUG
  Util::logging("==> while(true)");
#endif
  while (true)
  {
      int parse_triple_num = 0;
      _parser.parseFile(triple_array, parse_triple_num);
#ifdef DEBUG
      stringstream _ss;
      
      _ss << "finish rdfparser" << parse_triple_num << endl;
      Util::logging(_ss.str());
      cout << _ss.str() << endl;
#endif
      if (parse_triple_num == 0)
      {
          break;
      }

      
      
      
          
          
      

      
      success_num += this->insert(triple_array, parse_triple_num, _is_restore, txn);
      
      
  }

  delete[] triple_array;
  triple_array = NULL;
  long tv_insert = Util::get_cur_time();
  cout << "after insert, used " << (tv_insert - tv_load) << "ms." << endl;
  
  
  
  
      
  
  
  
  
      
  

  cout << "insert rdf triples done." << endl;
  cout<<"inserted triples num: "<<success_num<<endl;

  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  this->kvstore->set_if_single_thread(false);
  return true;
}

bool
Database::remove(std::string _rdf_file, bool _is_restore, shared_ptr<Transaction> txn)
{
  bool flag = _is_restore || this->load();
  
  if (!flag)
  {
      return false;
  }
  cout << "finish loading" << endl;

  long tv_load = Util::get_cur_time();
  TYPE_TRIPLE_NUM success_num = 0;

  ifstream _fin(_rdf_file.c_str());
  if (!_fin)
  {
      cout << "fail to open: " << _rdf_file << " in remove_test" << endl;
      return false;
  }

  
  TripleWithObjType* triple_array = new TripleWithObjType[RDFParser::TRIPLE_NUM_PER_GROUP];
  
  RDFParser _parser(_fin);

  
#ifdef DEBUG
  Util::logging("==> while(true)");
#endif
  while (true)
  {
      int parse_triple_num = 0;
      _parser.parseFile(triple_array, parse_triple_num);
#ifdef DEBUG
      stringstream _ss;
      
      _ss << "finish rdfparser" << parse_triple_num << endl;
      Util::logging(_ss.str());
      cout << _ss.str() << endl;
#endif
      if (parse_triple_num == 0)
      {
          break;
      }

      
      
      
          
          
      


      success_num += this->remove(triple_array, parse_triple_num, _is_restore, txn);
      
      
  }

  
  
  
  delete[] triple_array;
  triple_array = NULL;
  long tv_remove = Util::get_cur_time();
  cout << "after remove, used " << (tv_remove - tv_load) << "ms." << endl;

  
  
  
      
  
  
  
  
      
  

  cout << "remove rdf triples done." << endl;
  cout<<"removed triples num: "<<success_num<<endl;

  
  if(this->triples_num == 0)
  {
      this->resetIDinfo();
  }
  this->kvstore->set_if_single_thread(false);
  return true;
}

unsigned
Database::insert(const TripleWithObjType* _triples, TYPE_TRIPLE_NUM _triple_num, bool _is_restore, shared_ptr<Transaction> txn)
{
  vector<TYPE_ENTITY_LITERAL_ID> vertices, predicates;
  TYPE_TRIPLE_NUM valid_num = 0;

  if (!_is_restore) {
      string path = this->getStorePath() + '/' + this->update_log;
      string path_all = this->getStorePath() + '/' + this->update_log_since_backup;
      ofstream out;
      ofstream out_all;
      out.open(path.c_str(), ios::out | ios::app);
      out_all.open(path_all.c_str(), ios::out | ios::app);
      if (!out || !out_all) {
          cerr << "Failed to open update log. Insertion aborted." << endl;
          return 0;
      }
      for (int i = 0; i < _triple_num; i++) {
          if (exist_triple(_triples[i], txn)) {
              continue;
          }
          stringstream ss;
          ss << "I\t" << Util::node2string(_triples[i].getSubject().c_str()) << '\t';
          ss << Util::node2string(_triples[i].getPredicate().c_str()) << '\t';
          ss << Util::node2string(_triples[i].getObject().c_str()) << '\t' << Util::get_cur_time() << '.' << endl;
          out << ss.str();
          out_all << ss.str();
      }
      out.close();
      out_all.close();
  }

#ifdef USE_GROUP_INSERT
  
  
  TYPE_ENTITY_LITERAL_ID** id_tuples = new TYPE_ENTITY_LITERAL_ID*[_triple_num];

  
  int i = 0;
  
  
  
  
  map<int, EntityBitSet> old_sigmap;
  map<int, EntityBitSet> new_sigmap;
  set<int> new_entity;
  map<int, EntityBitSet>::iterator it;
  EntityBitSet tmpset;
  tmpset.reset();

  int subid, objid, preid;
  bool is_obj_entity;
  for (i = 0; i < _triple_num; ++i)
  {
      bool is_new_sub = false, is_new_pre = false, is_new_obj = false;

      string sub = _triples[i].getSubject();
      subid = this->kvstore->getIDByEntity(sub);
      if (subid == -1)
      {
          is_new_sub = true;
          subid = this->allocEntityID();
#ifdef DEBUG
          
#endif
          this->sub_num++;
          this->kvstore->setIDByEntity(sub, subid);
          this->kvstore->setEntityByID(subid, sub);
          new_entity.insert(subid);
          
          vertices.push_back(subid);
          
          
              
          
      }

      string pre = _triples[i].getPredicate();
      preid = this->kvstore->getIDByPredicate(pre);
      if (preid == -1)
      {
          is_new_pre = true;
          preid = this->allocPredicateID();
          this->kvstore->setIDByPredicate(pre, preid);
          this->kvstore->setPredicateByID(preid, pre);
          predicates.push_back(preid);
      }

      is_obj_entity = _triples[i].isObjEntity();
      string obj = _triples[i].getObject();
      if (is_obj_entity)
      {
          objid = this->kvstore->getIDByEntity(obj);
          if (objid == -1)
          {
              is_new_obj = true;
              objid = this->allocEntityID();
#ifdef DEBUG
              
#endif
              
              this->kvstore->setIDByEntity(obj, objid);
              this->kvstore->setEntityByID(objid, obj);
              new_entity.insert(objid);
              
              vertices.push_back(objid);
              
              
                  
              
          }
      }
      else 
      {
          objid = this->kvstore->getIDByLiteral(obj);
          if (objid == -1)
          {
              is_new_obj = true;
              objid = this->allocLiteralID();
              
              this->kvstore->setIDByLiteral(obj, objid);
              this->kvstore->setLiteralByID(objid, obj);
              
              vertices.push_back(objid);
              
              
              
                  
              
          }
      }

      bool triple_exist = false;
      if (!is_new_sub && !is_new_pre && !is_new_obj)
      {
          triple_exist = this->exist_triple(subid, preid, objid);
      }
      if (triple_exist)
      {
#ifdef DEBUG
          cout << "this triple exist" << endl;
#endif
          continue;
      }
#ifdef DEBUG
      cout << "this triple not exist" << endl;
#endif

      id_tuples[valid_num] = new int[3];
      id_tuples[valid_num][0] = subid;
      id_tuples[valid_num][1] = preid;
      id_tuples[valid_num][2] = objid;
      this->triples_num++;
      valid_num++;

      tmpset.reset();
      Signature::encodePredicate2Entity(preid, tmpset, Util::EDGE_OUT);
      Signature::encodeStr2Entity(obj.c_str(), tmpset);
      if (new_entity.find(subid) != new_entity.end())
      {
          it = new_sigmap.find(subid);
          if (it != new_sigmap.end())
          {
              it->second |= tmpset;
          }
          else
          {
              new_sigmap[subid] = tmpset;
          }
      }
      else
      {
          it = old_sigmap.find(subid);
          if (it != old_sigmap.end())
          {
              it->second |= tmpset;
          }
          else
          {
              old_sigmap[subid] = tmpset;
          }
      }

      if (is_obj_entity)
      {
          tmpset.reset();
          Signature::encodePredicate2Entity(preid, tmpset, Util::EDGE_IN);
          Signature::encodeStr2Entity(sub.c_str(), tmpset);
          if (new_entity.find(objid) != new_entity.end())
          {
              it = new_sigmap.find(objid);
              if (it != new_sigmap.end())
              {
                  it->second |= tmpset;
              }
              else
              {
                  new_sigmap[objid] = tmpset;
              }
          }
          else
          {
              it = old_sigmap.find(objid);
              if (it != old_sigmap.end())
              {
                  it->second |= tmpset;
              }
              else
              {
                  old_sigmap[objid] = tmpset;
              }
          }
      }
  }

#ifdef DEBUG
  cout << "old sigmap size: " << old_sigmap.size() << endl;
  cout << "new sigmap size: " << new_sigmap.size() << endl;
  cout << "valid num: " << valid_num << endl;
#endif

  
  
  

  
  
  
  
  
  
  
  {
#ifdef DEBUG
      cout << "INSRET PROCESS: to spo cmp and update" << endl;
#endif
#ifndef PARALLEL_SORT
      qsort(id_tuples, valid_num, sizeof(int*), KVstore::_spo_cmp);
#else
      omp_set_num_threads(thread_num);
      __gnu_parallel::sort(id_tuples, id_tuples + valid_num, KVstore::parallel_spo_cmp);
#endif

      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      

      vector<int> oidlist_s;
      vector<int> pidlist_s;
      vector<int> oidlist_sp;
      vector<int> pidoidlist_s;

      bool _sub_change = true;
      bool _sub_pre_change = true;
      bool _pre_change = true;

      for (int i = 0; i < valid_num; ++i)
          if (i + 1 == valid_num || (id_tuples[i][0] != id_tuples[i + 1][0] || id_tuples[i][1] != id_tuples[i + 1][1] || id_tuples[i][2] != id_tuples[i + 1][2]))
          {
              int _sub_id = id_tuples[i][0];
              int _pre_id = id_tuples[i][1];
              int _obj_id = id_tuples[i][2];

              oidlist_s.push_back(_obj_id);
              oidlist_sp.push_back(_obj_id);
              pidoidlist_s.push_back(_pre_id);
              pidoidlist_s.push_back(_obj_id);
              pidlist_s.push_back(_pre_id);

              _sub_change = (i + 1 == valid_num) || (id_tuples[i][0] != id_tuples[i + 1][0]);
              _pre_change = (i + 1 == valid_num) || (id_tuples[i][1] != id_tuples[i + 1][1]);
              _sub_pre_change = _sub_change || _pre_change;

              if (_sub_pre_change)
              {
#ifdef DEBUG
                  cout << "update sp2o: " << _sub_id << " " << _pre_id << " " << oidlist_sp.size() << endl;
#endif
                  cout << this->kvstore->getEntityByID(_sub_id) << endl;
                  cout << this->kvstore->getPredicateByID(_pre_id) << endl;
                  
                  oidlist_sp.clear();
              }

              if (_sub_change)
              {
#ifdef DEBUG
                  cout << "update s2p: " << _sub_id << " " << pidlist_s.size() << endl;
#endif
                  
                  pidlist_s.clear();

#ifdef DEBUG
                  cout << "update s2po: " << _sub_id << " " << pidoidlist_s.size() << endl;
#endif
                  this->kvstore->updateInsert_s2values(_sub_id, pidoidlist_s);
                  pidoidlist_s.clear();

#ifdef DEBUG
                  cout << "update s2o: " << _sub_id << " " << oidlist_s.size() << endl;
#endif
#ifndef PARALLEL_SORT
                  sort(oidlist_s.begin(), oidlist_s.end());
#else
                  omp_set_num_threads(thread_num);
                  __gnu_parallel::sort(oidlist_s.begin(), oidlist_s.end());
#endif
                  
                  oidlist_s.clear();
              }

          }
#ifdef DEBUG
      cout << "INSERT PROCESS: OUT s2po..." << endl;
#endif
  }
  
  {
#ifdef DEBUG
      cout << "INSRET PROCESS: to ops cmp and update" << endl;
#endif
#ifndef PARALLEL_SORT
      qsort(id_tuples, valid_num, sizeof(int**), KVstore::_ops_cmp);
#else
      omp_set_num_threads(thread_num);
      __gnu_parallel::sort(id_tuples, id_tuples + valid_num, KVstore::parallel_ops_cmp);
#endif
      vector<int> sidlist_o;
      vector<int> sidlist_op;
      vector<int> pidsidlist_o;
      vector<int> pidlist_o;

      bool _obj_change = true;
      bool _pre_change = true;
      bool _obj_pre_change = true;

      for (int i = 0; i < valid_num; ++i)
          if (i + 1 == valid_num || (id_tuples[i][0] != id_tuples[i + 1][0] || id_tuples[i][1] != id_tuples[i + 1][1] || id_tuples[i][2] != id_tuples[i + 1][2]))
          {
              int _sub_id = id_tuples[i][0];
              int _pre_id = id_tuples[i][1];
              int _obj_id = id_tuples[i][2];

              sidlist_o.push_back(_sub_id);
              sidlist_op.push_back(_sub_id);
              pidsidlist_o.push_back(_pre_id);
              pidsidlist_o.push_back(_sub_id);
              pidlist_o.push_back(_pre_id);

              _obj_change = (i + 1 == valid_num) || (id_tuples[i][2] != id_tuples[i + 1][2]);
              _pre_change = (i + 1 == valid_num) || (id_tuples[i][1] != id_tuples[i + 1][1]);
              _obj_pre_change = _obj_change || _pre_change;

              if (_obj_pre_change)
              {
#ifdef DEBUG
                  cout << "update op2s: " << _obj_id << " " << _pre_id << " " << sidlist_op.size() << endl;
#endif
                  
                  sidlist_op.clear();
              }

              if (_obj_change)
              {
#ifdef DEBUG
                  cout << "update o2s: " << _obj_id << " " << sidlist_o.size() << endl;
#endif
#ifndef PARALLEL_SORT
                  sort(sidlist_o.begin(), sidlist_o.end());
#else
                  omp_set_num_threads(thread_num);
                  __gnu_parallel::sort(sidlist_o.begin(), sidlist_o.end());
#endif
                  
                  sidlist_o.clear();

#ifdef DEBUG
                  cout << "update o2ps: " << _obj_id << " " << pidsidlist_o.size() << endl;
#endif
                  this->kvstore->updateInsert_o2values(_obj_id, pidsidlist_o);
                  pidsidlist_o.clear();

#ifdef DEBUG
                  cout << "update o2p: " << _obj_id << " " << pidlist_o.size() << endl;
#endif
                  
                  pidlist_o.clear();
              }

          }
#ifdef DEBUG
      cout << "INSERT PROCESS: OUT o2ps..." << endl;
#endif
  }
  
  {
#ifdef DEBUG
      cout << "INSRET PROCESS: to pso cmp and update" << endl;
#endif
#ifndef PARALLEL_SORT
      qsort(id_tuples, valid_num, sizeof(int*), KVstore::_pso_cmp);
#else
      omp_set_num_threads(thread_num);
      __gnu_parallel::sort(id_tuples, id_tuples + valid_num,  KVstore::parallel_pso_cmp);
#endif
      vector<int> sidlist_p;
      vector<int> oidlist_p;
      vector<int> sidoidlist_p;

      bool _pre_change = true;
      bool _sub_change = true;
      

      for (int i = 0; i < valid_num; i++)
          if (i + 1 == valid_num || (id_tuples[i][0] != id_tuples[i + 1][0] || id_tuples[i][1] != id_tuples[i + 1][1] || id_tuples[i][2] != id_tuples[i + 1][2]))
          {
              int _sub_id = id_tuples[i][0];
              int _pre_id = id_tuples[i][1];
              int _obj_id = id_tuples[i][2];

              oidlist_p.push_back(_obj_id);
              sidoidlist_p.push_back(_sub_id);
              sidoidlist_p.push_back(_obj_id);
              sidlist_p.push_back(_sub_id);

              _pre_change = (i + 1 == valid_num) || (id_tuples[i][1] != id_tuples[i + 1][1]);
              _sub_change = (i + 1 == valid_num) || (id_tuples[i][0] != id_tuples[i + 1][0]);
              

              if (_pre_change)
              {
#ifdef DEBUG
                  cout << "update p2s: " << _pre_id << " " << sidlist_p.size() << endl;
#endif
                  
                  sidlist_p.clear();

#ifdef DEBUG
                  cout << "update p2o: " << _pre_id << " " << oidlist_p.size() << endl;
#endif
#ifndef PARALLEL_SORT
                  sort(oidlist_p.begin(), oidlist_p.end());
#else
                  omp_set_num_threads(thread_num);
                  __gnu_parallel::sort(oidlist_p.begin(), oidlist_p.end());
#endif
                  
                  oidlist_p.clear();

#ifdef DEBUG
                  cout << "update p2so: " << _pre_id << " " << sidoidlist_p.size() << endl;
#endif
                  this->kvstore->updateInsert_p2values(_pre_id, sidoidlist_p);
                  sidoidlist_p.clear();
              }
          }
#ifdef DEBUG
      cout << "INSERT PROCESS: OUT p2so..." << endl;
#endif
  }


  for (int i = 0; i < valid_num; ++i)
  {
      delete[] id_tuples[i];
  }
  delete[] id_tuples;
  id_tuples = NULL;

  
  
      
  
  
  
      
      
  
#else 
  
  
  for (TYPE_TRIPLE_NUM i = 0; i < _triple_num; ++i)
  {
      bool ret = this->insertTriple(_triples[i], &vertices, &predicates, txn);
      if(ret)
      {
          valid_num++;
      }
  }
#endif 

  this->stringindex->SetTrie(kvstore->getTrie());
  
  this->stringindex->change(vertices, *this->kvstore, true);
  this->stringindex->change(predicates, *this->kvstore, false);

  return valid_num;
}

unsigned
Database::remove(const TripleWithObjType* _triples, TYPE_TRIPLE_NUM _triple_num, bool _is_restore, shared_ptr<Transaction> txn)
{
  vector<TYPE_ENTITY_LITERAL_ID> vertices, predicates;
  TYPE_TRIPLE_NUM valid_num = 0;

  if (!_is_restore) {
      string path = this->getStorePath() + '/' + this->update_log;
      string path_all = this->getStorePath() + '/' + this->update_log_since_backup;
      ofstream out;
      ofstream out_all;
      out.open(path.c_str(), ios::out | ios::app);
      out_all.open(path_all.c_str(), ios::out | ios::app);
      if (!out || !out_all) {
          cerr << "Failed to open update log. Removal aborted." << endl;
          return 0;
      }
      for (int i = 0; i < _triple_num; i++) {
          if (!exist_triple(_triples[i], txn)) {
              continue;
          }
          stringstream ss;
          ss << "R\t" << Util::node2string(_triples[i].getSubject().c_str()) << '\t';
          ss << Util::node2string(_triples[i].getPredicate().c_str()) << '\t';
          ss << Util::node2string(_triples[i].getObject().c_str()) << '\t' << Util::get_cur_time() << '.' << endl;
          out << ss.str();
          out_all << ss.str();
      }
      out.close();
      out_all.close();
  }

#ifdef USE_GROUP_DELETE
  
  
  TYPE_ENTITY_LITERAL_ID** id_tuples = new TYPE_ENTITY_LITERAL_ID*[_triple_num];
  TYPE_TRIPLE_NUM i = 0;

  

  
  
  
  
  
  
  EntityBitSet tmpset;
  tmpset.reset();

  TYPE_ENTITY_LITERAL_ID subid, objid;
  TYPE_PREDICATE_ID preid;
  bool is_obj_entity;
  for (i = 0; i < _triple_num; ++i)
  {
      string sub = _triples[i].getSubject();
      subid = this->kvstore->getIDByEntity(sub);
      
      if(subid == INVALID_ENTITY_LITERAL_ID)
      {
          continue;
      }

      string pre = _triples[i].getPredicate();
      preid = this->kvstore->getIDByPredicate(pre);
      
      if(preid == INVALID_PREDICATE_ID)
      {
          continue;
      }

      is_obj_entity = _triples[i].isObjEntity();
      string obj = _triples[i].getObject();
      if (is_obj_entity)
      {
          objid = this->kvstore->getIDByEntity(obj);
      }
      else 
      {
          objid = this->kvstore->getIDByLiteral(obj);
      }
      
      if(objid == INVALID_ENTITY_LITERAL_ID)
      {
          continue;
      }

      
      
          
      
      bool _exist_triple = this->exist_triple(subid, preid, objid);
      if (!_exist_triple)
      {
          continue;
      }

      id_tuples[valid_num] = new TYPE_ENTITY_LITERAL_ID[3];
      id_tuples[valid_num][0] = subid;
      id_tuples[valid_num][1] = preid;
      id_tuples[valid_num][2] = objid;
      this->triples_num--;
      valid_num++;
  }

  
  

  int sub_degree, obj_degree, pre_degree;
  string tmpstr;
  
  
  
  
  
  
  {
#ifdef DEBUG
      cout << "INSRET PROCESS: to spo cmp and update" << endl;
#endif
#ifndef PARALLEL_SORT
      qsort(id_tuples, valid_num, sizeof(int*), KVstore::_spo_cmp);
#else
      omp_set_num_threads(thread_num);
      __gnu_parallel::sort(id_tuples, id_tuples + valid_num, KVstore::parallel_spo_cmp);
#endif
      vector<int> oidlist_s;
      vector<int> pidlist_s;
      vector<int> oidlist_sp;
      vector<int> pidoidlist_s;

      bool _sub_change = true;
      bool _sub_pre_change = true;
      bool _pre_change = true;

      for (int i = 0; i < valid_num; ++i)
          if (i + 1 == valid_num || (id_tuples[i][0] != id_tuples[i + 1][0] || id_tuples[i][1] != id_tuples[i + 1][1] || id_tuples[i][2] != id_tuples[i + 1][2]))
          {
              int _sub_id = id_tuples[i][0];
              int _pre_id = id_tuples[i][1];
              int _obj_id = id_tuples[i][2];

              oidlist_s.push_back(_obj_id);
              oidlist_sp.push_back(_obj_id);
              pidoidlist_s.push_back(_pre_id);
              pidoidlist_s.push_back(_obj_id);
              pidlist_s.push_back(_pre_id);

              _sub_change = (i + 1 == valid_num) || (id_tuples[i][0] != id_tuples[i + 1][0]);
              _pre_change = (i + 1 == valid_num) || (id_tuples[i][1] != id_tuples[i + 1][1]);
              _sub_pre_change = _sub_change || _pre_change;

              if (_sub_pre_change)
              {
                  this->kvstore->updateRemove_sp2o(_sub_id, _pre_id, oidlist_sp);
                  oidlist_sp.clear();
              }

              if (_sub_change)
              {
                  this->kvstore->updateRemove_s2p(_sub_id, pidlist_s);
                  pidlist_s.clear();
                  this->kvstore->updateRemove_s2po(_sub_id, pidoidlist_s);
                  pidoidlist_s.clear();

#ifndef PARALLEL_SORT
                  sort(oidlist_s.begin(), oidlist_s.end());
#else
                  omp_set_num_threads(thread_num);
                  __gnu_parallel::sort(oidlist_s.begin(), oidlist_s.end());
#endif
                  this->kvstore->updateRemove_s2o(_sub_id, oidlist_s);
                  oidlist_s.clear();

                  sub_degree = (this->kvstore)->getEntityDegree(_sub_id);
                  if (sub_degree == 0)
                  {
                      tmpstr = this->kvstore->getEntityByID(_sub_id);
                      this->kvstore->subEntityByID(_sub_id);
                      this->kvstore->subIDByEntity(tmpstr);
                      
                      this->freeEntityID(_sub_id);
                      this->sub_num--;
                      
                      vertices.push_back(_sub_id);
                      
                      
                          
                      
                  }
                  else
                  {
                      tmpset.reset();
                      this->calculateEntityBitSet(_sub_id, tmpset);
                      
                  }
              }

          }
#ifdef DEBUG
      cout << "INSERT PROCESS: OUT s2po..." << endl;
#endif
  }
  
  {
#ifdef DEBUG
      cout << "INSRET PROCESS: to ops cmp and update" << endl;
#endif
#ifndef PARALLEL_SORT
      qsort(id_tuples, valid_num, sizeof(int**), KVstore::_ops_cmp);
#else
      omp_set_num_threads(thread_num);
      __gnu_parallel::sort(id_tuples, id_tuples + valid_num, KVstore::parallel_ops_cmp);
#endif
      vector<int> sidlist_o;
      vector<int> sidlist_op;
      vector<int> pidsidlist_o;
      vector<int> pidlist_o;

      bool _obj_change = true;
      bool _pre_change = true;
      bool _obj_pre_change = true;

      for (int i = 0; i < valid_num; ++i)
          if (i + 1 == valid_num || (id_tuples[i][0] != id_tuples[i + 1][0] || id_tuples[i][1] != id_tuples[i + 1][1] || id_tuples[i][2] != id_tuples[i + 1][2]))
          {
              int _sub_id = id_tuples[i][0];
              int _pre_id = id_tuples[i][1];
              int _obj_id = id_tuples[i][2];

              sidlist_o.push_back(_sub_id);
              sidlist_op.push_back(_sub_id);
              pidsidlist_o.push_back(_pre_id);
              pidsidlist_o.push_back(_sub_id);
              pidlist_o.push_back(_pre_id);

              _obj_change = (i + 1 == valid_num) || (id_tuples[i][2] != id_tuples[i + 1][2]);
              _pre_change = (i + 1 == valid_num) || (id_tuples[i][1] != id_tuples[i + 1][1]);
              _obj_pre_change = _obj_change || _pre_change;

              if (_obj_pre_change)
              {
                  this->kvstore->updateRemove_op2s(_obj_id, _pre_id, sidlist_op);
                  sidlist_op.clear();
              }

              if (_obj_change)
              {
#ifndef PARALLEL_SORT
                  sort(sidlist_o.begin(), sidlist_o.end());
#else
                  omp_set_num_threads(thread_num);
                  __gnu_parallel::sort(sidlist_o.begin(), sidlist_o.end());
#endif
                  this->kvstore->updateRemove_o2s(_obj_id, sidlist_o);
                  sidlist_o.clear();
                  this->kvstore->updateRemove_o2ps(_obj_id, pidsidlist_o);
                  pidsidlist_o.clear();

                  this->kvstore->updateRemove_o2p(_obj_id, pidlist_o);
                  pidlist_o.clear();

                  is_obj_entity = this->objIDIsEntityID(_obj_id);
                  if (is_obj_entity)
                  {
                      obj_degree = this->kvstore->getEntityDegree(_obj_id);
                      if (obj_degree == 0)
                      {
                          tmpstr = this->kvstore->getEntityByID(_obj_id);
                          this->kvstore->subEntityByID(_obj_id);
                          this->kvstore->subIDByEntity(tmpstr);
                          
                          this->freeEntityID(_obj_id);
                          
                          vertices.push_back(_obj_id);
                          
                          
                              
                          
                      }
                      else
                      {
                          tmpset.reset();
                          
                          
                      }
                  }
                  else
                  {
                      obj_degree = this->kvstore->getLiteralDegree(_obj_id);
                      if (obj_degree == 0)
                      {
                          tmpstr = this->kvstore->getLiteralByID(_obj_id);
                          this->kvstore->subLiteralByID(_obj_id);
                          this->kvstore->subIDByLiteral(tmpstr);
                          this->freeLiteralID(_obj_id);
                          
                          vertices.push_back(_obj_id);
                          
                          
                          
                              
                          
                      }
                  }
              }

          }
#ifdef DEBUG
      cout << "INSERT PROCESS: OUT o2ps..." << endl;
#endif
  }
  
  {
#ifdef DEBUG
      cout << "INSRET PROCESS: to pso cmp and update" << endl;
#endif
#ifndef PARALLEL_SORT
      qsort(id_tuples, valid_num, sizeof(int*), KVstore::_pso_cmp);
#else
      omp_set_num_threads(thread_num);
      __gnu_parallel::sort(id_tuples, id_tuples + valid_num, KVstore::parallel_pso_cmp);
#endif
      vector<int> sidlist_p;
      vector<int> oidlist_p;
      vector<int> sidoidlist_p;

      bool _pre_change = true;
      bool _sub_change = true;
      

      for (int i = 0; i < valid_num; i++)
          if (i + 1 == valid_num || (id_tuples[i][0] != id_tuples[i + 1][0] || id_tuples[i][1] != id_tuples[i + 1][1] || id_tuples[i][2] != id_tuples[i + 1][2]))
          {
              int _sub_id = id_tuples[i][0];
              int _pre_id = id_tuples[i][1];
              int _obj_id = id_tuples[i][2];

              oidlist_p.push_back(_obj_id);
              sidoidlist_p.push_back(_sub_id);
              sidoidlist_p.push_back(_obj_id);
              sidlist_p.push_back(_sub_id);

              _pre_change = (i + 1 == valid_num) || (id_tuples[i][1] != id_tuples[i + 1][1]);
              _sub_change = (i + 1 == valid_num) || (id_tuples[i][0] != id_tuples[i + 1][0]);
              

              if (_pre_change)
              {
                  this->kvstore->updateRemove_p2s(_pre_id, sidlist_p);
                  sidlist_p.clear();

#ifndef PARALLEL_SORT
                  sort(oidlist_p.begin(), oidlist_p.end());
#else
                  omp_set_num_threads(thread_num);
                  __gnu_parallel::sort(oidlist_p.begin(), oidlist_p.end());
#endif
                  this->kvstore->updateRemove_p2o(_pre_id, oidlist_p);
                  oidlist_p.clear();

                  this->kvstore->updateRemove_p2so(_pre_id, sidoidlist_p);
                  sidoidlist_p.clear();

                  pre_degree = this->kvstore->getPredicateDegree(_pre_id);
                  if (pre_degree == 0)
                  {
                      tmpstr = this->kvstore->getPredicateByID(_pre_id);
                      this->kvstore->subPredicateByID(_pre_id);
                      this->kvstore->subIDByPredicate(tmpstr);
                      this->freePredicateID(_pre_id);
                      
                      predicates.push_back(_pre_id);
                  }
              }
          }
#ifdef DEBUG
      cout << "INSERT PROCESS: OUT p2so..." << endl;
#endif
  }


  for (int i = 0; i < valid_num; ++i)
  {
      delete[] id_tuples[i];
  }
  delete[] id_tuples;
  id_tuples = NULL;
#else
  
  
  for (TYPE_TRIPLE_NUM i = 0; i < _triple_num; ++i)
  {
      bool ret = this->removeTriple(_triples[i], &vertices, &predicates, txn);
      if(ret)
      {
          valid_num++;
      }
  }
#endif
  if(txn != nullptr)
  {
      this->stringindex->SetTrie(kvstore->getTrie());
      
      this->stringindex->disable(vertices, true);
      this->stringindex->disable(predicates, false);
  }
  
  
  
  if(this->triples_num == 0)
  {
      this->resetIDinfo();
  }

  return valid_num;
}

bool
Database::backup()
{
  if (!Util::dir_exist(Util::backup_path))
  {
      Util::create_dir(Util::backup_path);
  }
  string backup_path = Util::backup_path + this->store_path;

  cout << "Beginning backup." << endl;

  string sys_cmd;
  if (Util::dir_exist(backup_path))
  {
      sys_cmd = "rm -rf " + backup_path;
      system(sys_cmd.c_str());
  }
  sys_cmd = "cp -r " + this->store_path + ' ' + backup_path;
  system(sys_cmd.c_str());
  sys_cmd = "rm " + backup_path + '/' + this->update_log;
  system(sys_cmd.c_str());

  
  this->kvstore->flush();

  this->clear_update_log();
  string update_log_path = this->store_path + '/' + this->update_log_since_backup;
  sys_cmd = "rm " + update_log_path;
  system(sys_cmd.c_str());
  Util::create_file(update_log_path);

  cout << "Backup completed!" << endl;
  return true;
}

bool
Database::restore()
{
  cout << "Begining restore." << endl;
  string sys_cmd;

  multiset<string> insertions;
  multiset<string> removals;

  int num_update = 0;
  if (!this->load())
  {
      this->clear();

      string backup_path = Util::backup_path + this->store_path;
      if (!Util::dir_exist(Util::backup_path))
      {
          cerr << "Failed to restore!" << endl;
          return false;
      }

      num_update += Database::read_update_log(this->store_path + '/' + this->update_log_since_backup, insertions, removals);

      cout << "Failed to restore from original db file, trying to restore from backup file." << endl;
      cout << "Your old db file will be stored at " << this->store_path << ".bad" << endl;

      sys_cmd = "rm -rf " + this->store_path + ".bad";
      system(sys_cmd.c_str());
      sys_cmd = "cp -r " + this->store_path + ' ' + this->store_path + ".bad";
      system(sys_cmd.c_str());
      sys_cmd = "rm -rf " + this->store_path;
      system(sys_cmd.c_str());
      sys_cmd = "cp -r " + backup_path + ' ' + this->store_path;
      system(sys_cmd.c_str());
      Util::create_file(this->store_path + '/' + this->update_log);

      if (!this->load())
      {
          this->clear();
          cerr << "Failed to restore from backup file." << endl;
          return false;
      }

      num_update += Database::read_update_log(this->store_path + '/' + this->update_log_since_backup, insertions, removals);
  }
  else
  {
      num_update += Database::read_update_log(this->store_path + '/' + this->update_log, insertions, removals);
  }

  cout << "Restoring " << num_update << " updates." << endl;

  if (!this->restore_update(insertions, removals))
  {
      cerr << "Failed to restore updates" << endl;
      return false;
  }

  cout << "Restore completed." << endl;

  return true;
}

int
Database::read_update_log(const string _path, multiset<string>& _i, multiset<string>& _r)
{
  ifstream in;
#ifdef DEBUG
  cout<<_path<<endl;
#endif
  in.open(_path.c_str(), ios::in);
  if (!in) {
      cerr << "Failed to read update log." << endl;
      return 0;
  }

  int ret = 0;
  int buffer_size = 1024 + 2;
  char buffer[buffer_size];
  in.getline(buffer, buffer_size);
  while (!in.eof() && buffer[0]) {
      string triple;
      switch (buffer[0]) {
      case 'I':
          triple = string(buffer + 2);
          ret++;
          _i.insert(triple);
          break;
      case 'R':
          triple = string(buffer + 2);
          ret++;
          _r.insert(triple);
          break;
      default:
          cerr << "Bad line in update log!" << endl;
      }
      in.getline(buffer, buffer_size);
  }

  return ret;
}

bool
Database::restore_update(multiset<string>& _i, multiset<string>& _r)
{
  multiset<string>::iterator pos;
  multiset<string>::iterator it_to_erase = _r.end();
  for (multiset<string>::iterator it = _r.begin(); it != _r.end(); it++)
  {
      
      if (it_to_erase != _r.end()) {
          _r.erase(it_to_erase);
      }
      pos = _i.find(*it);
      if (pos != _i.end()) {
          _i.erase(pos);
          it_to_erase = it;
      }
      else {
          it_to_erase = _r.end();
      }
  }
  if (it_to_erase != _r.end()) {
      _r.erase(it_to_erase);
  }

  string tmp_path = this->store_path + "/.update_tmp";

  ofstream out_i;
  out_i.open(tmp_path.c_str(), ios::out);
  if (!out_i) {
      cerr << "Failed to open temp file, restore failed!" << endl;
      return false;
  }
  for (multiset<string>::iterator it = _i.begin(); it != _i.end(); it++) {
      out_i << *it << endl;
  }
  out_i.close();
  if (!this->insert(tmp_path, true))
  
  {
      return false;
  }

  ofstream out_r;
  out_r.open(tmp_path.c_str(), ios::out);
  if (!out_r) {
      cerr << "Failed to open temp file!" << endl;
      return false;
  }
  for (multiset<string>::iterator it = _r.begin(); it != _r.end(); it++) {
      out_r << *it << endl;
  }
  out_r.close();
  if (!this->remove(tmp_path, true))
  
  {
      return false;
  }

  string cmd = "rm " + tmp_path;
  system(cmd.c_str());
  return true;
}

void
Database::clear_update_log()
{
  string cmd = "rm " + this->store_path + '/' + this->update_log;
  system(cmd.c_str());
  Util::create_file(this->store_path + '/' + this->update_log);
}

bool
Database::objIDIsEntityID(TYPE_ENTITY_LITERAL_ID _id)
{
  return _id < Util::LITERAL_FIRST_ID;
}

bool
Database::getFinalResult(SPARQLquery& _sparql_q, ResultSet& _result_set)
{
#ifdef DEBUG_PRECISE
  printf("getFinalResult:begins\n");
#endif
  
  int _var_num = _sparql_q.getQueryVarNum();
  _result_set.setVar(_sparql_q.getQueryVar());
  vector<BasicQuery*>& query_vec = _sparql_q.getBasicQueryVec();

  
  unsigned _ans_num = 0;
#ifdef DEBUG_PRECISE
  printf("getFinalResult:before ansnum loop\n");
#endif
  for (unsigned i = 0; i < query_vec.size(); i++)
  {
      _ans_num += query_vec[i]->getResultList().size();
  }
#ifdef DEBUG_PRECISE
  printf("getFinalResult:after ansnum loop\n");
#endif

  _result_set.ansNum = _ans_num;
#ifndef STREAM_ON
  _result_set.answer = new string*[_ans_num];
  for (unsigned i = 0; i < _result_set.ansNum; i++)
  {
      _result_set.answer[i] = NULL;
  }
#else
  vector<unsigned> keys;
  vector<bool> desc;
  _result_set.openStream(keys, desc);
  
#ifdef DEBUG_PRECISE
  printf("getFinalResult:after open stream\n");
#endif
#endif
#ifdef DEBUG_PRECISE
  printf("getFinalResult:before main loop\n");
#endif
  unsigned tmp_ans_count = 0;
  
  
  for (unsigned i = 0; i < query_vec.size(); i++)
  {
      vector<unsigned*>& tmp_vec = query_vec[i]->getResultList();
      
      
      
      for (vector<unsigned*>::iterator itr = tmp_vec.begin(); itr != tmp_vec.end(); ++itr)
      {
          
#ifndef STREAM_ON
          _result_set.answer[tmp_ans_count] = new string[_var_num];
#endif
#ifdef DEBUG_PRECISE
          printf("getFinalResult:before map loop\n");
#endif
          
          
          
          
          
          for (int v = 0; v < _var_num; ++v)
          {
              unsigned ans_id = (*itr)[v];
              string ans_str;
              if (this->objIDIsEntityID(ans_id))
              {
                  ans_str = (this->kvstore)->getEntityByID(ans_id);
              }
              else
              {
                  ans_str = (this->kvstore)->getLiteralByID(ans_id);
              }
#ifndef STREAM_ON
              _result_set.answer[tmp_ans_count][v] = ans_str;
#else
              _result_set.writeToStream(ans_str);
#endif
#ifdef DEBUG_PRECISE
              printf("getFinalResult:after copy/write\n");
#endif
          }
          tmp_ans_count++;
      }
  }
#ifdef STREAM_ON
  _result_set.resetStream();
#endif
#ifdef DEBUG_PRECISE
  printf("getFinalResult:ends\n");
#endif

  return true;
}




  
  
  
      
  
  
  





  
  
  
      
  
  
  





  

  
  
  
      
      

          
          
          
              
              
              
          

          
          
          
          
              
              
              
          
      

      
      
          
          
          
          
              
              
              
          
      
  
  
      
      
          
          
              
                  
              
              
                  
                  
                  
                  
              

              
                  
              
              
                  
                  
                  
                  
              
          
      

      
      
          
          
          
              
                  
              
              
                  
                  
                  
                      
                  
              
          
      
  
  
      
      
          
          
          
              
              
              
          
      

      
      
          
          
          
              
              
          
      

      
      
          
          
          
          
              
                  
              
          
      
  





  
  














	
		
		
	
	
		
		
	
	
		
		
	
	
		
		
	
	
	
	

	
	
	
	

	

	
	
























































void
Database::version_clean()
{
	vector<unsigned> sub_ids , obj_ids, obj_literal_ids, pre_ids;
	(this->kvstore)->IVArray_Vacuum(sub_ids, obj_ids, obj_literal_ids, pre_ids);
	vector<TYPE_ENTITY_LITERAL_ID> vertices, predicates;
	
	int sub_degree, obj_degree, pre_degree;
	for(auto &_sub_id: sub_ids)
	{
		sub_degree = (this->kvstore)->getEntityDegree(_sub_id);
		
		if (sub_degree == 0)
		{
			
			
			this->kvstore->subIDByEntity(this->kvstore->getEntityByID(_sub_id));
			this->kvstore->subEntityByID(_sub_id);			
			this->freeEntityID(_sub_id);
			this->sub_num--;
			
			
			
				
			
			vertices.push_back(_sub_id);
		}
		
	}
	
	for(auto &_obj_id: obj_ids)
	{
		obj_degree = this->kvstore->getEntityDegree(_obj_id);
		if (obj_degree == 0)
		{
			
			
			this->kvstore->subIDByEntity(this->kvstore->getEntityByID(_obj_id));
			this->kvstore->subEntityByID(_obj_id);			
			this->freeEntityID(_obj_id);
			
			
			
				
			
			vertices.push_back(_obj_id);
		}
	}
	
	for(auto &_obj_id: obj_literal_ids)
	{
		obj_degree = this->kvstore->getLiteralDegree(_obj_id);
		if (obj_degree == 0)
		{
			this->kvstore->subIDByLiteral(this->kvstore->getLiteralByID(_obj_id));
			this->kvstore->subLiteralByID(_obj_id);
			
			this->freeLiteralID(_obj_id);
			
			
			
			
			
			
			vertices.push_back(_obj_id);
		}
	}
		
	for(auto &_pre_id: pre_ids)
	{
		pre_degree = this->kvstore->getPredicateDegree(_pre_id);
		if (pre_degree == 0)
		{
			this->kvstore->subIDByPredicate(this->kvstore->getPredicateByID(_pre_id));
			this->kvstore->subPredicateByID(_pre_id);
			this->freePredicateID(_pre_id);
			predicates.push_back(_pre_id);
		}
		
	}
	
	
	
	this->stringindex->SetTrie(kvstore->getTrie());
	
	this->stringindex->disable(vertices, true);
	this->stringindex->disable(predicates, false);
	
}

void 
Database::transaction_rollback(shared_ptr<Transaction> txn)
{
	if((this->kvstore)->transaction_invalid(txn) == false)
	{
		cerr << "WARNING: transaction rollback exception! " << endl;
		cerr << "Please REBOOT service!" << endl;
	}
}

void 
Database::transaction_commit(shared_ptr<Transaction> txn)
{
	
	if((this->kvstore)->releaseAllLatches(txn) == false)
	{
		cerr << "WARNING: not all latches get unlatched! " << endl;
		cerr << "Please REBOOT service!" << endl;
	}
	
	
	
	
	
}

void Database::load_statistics() {
    this->statistics = new Statistics(this->getStorePath(), this->getlimitID_predicate());
    this->statistics->load_Statistics();
}
