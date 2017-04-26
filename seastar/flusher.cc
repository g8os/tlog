#include "flusher.h"

#include "core/sleep.hh"

// capnp
#include "tlog_schema.capnp.h"
#include <capnp/message.h>
#include <kj/io.h>
#include <kj/common.h>
#include <capnp/serialize-packed.h>

// other lib
#include <blake2.h>
#include <isa-l/erasure_code.h>
#include <snappy.h>
#include <isa-l_crypto/aes_cbc.h>
#include <isa-l_crypto/aes_keyexp.h>

#include "redis_conn.h"

static int flush_count = 0;

static std::vector<Flusher *> _flushers(100);
/* save last hash to memory */
static std::map<uint32_t, uint8_t *> g_last_hash;

/* len of blake2b hash we want to generate */
static const unsigned int HASH_LEN = 32;

/* number of extra bytes for capnp aggregation encoding
 * TODO : find the correct number. It currently based only
 * on my own guest.
 * */
static const int CAPNP_OUTBUF_EXTRA = 200;

Flusher::Flusher(std::string objstor_addr, int objstor_port, std::string priv_key, 
		int flush_size, int flush_timeout, int k, int m)
	: _flush_size(flush_size)
	, _flush_timeout(flush_timeout)
	, _k(k)
	, _m(m)
	, _objstor_addr(objstor_addr)
	, _objstor_port(objstor_port)
	, _priv_key(priv_key)
{
	_er = new Erasurer(_k, _m);

	// initialize redis_conns vector
	_redis_conns.reserve(_k + _m + 1);
	_redis_conns.resize(_k + _m + 1);

	// encryption input vector
	std::memset(_enc_iv, '0', 16);

	// encryption key
	uint8_t dec_key[256];
	aes_keyexp_256((uint8_t *)priv_key.c_str(), _enc_key, dec_key);
}

/**
 * post_init contains steps that (for unknown reason)
 * can't be put inside the class constructor
 */
void Flusher::post_init() {
	_flushers[engine().cpu_id()] = this;
	init_redis_conns();
}

void Flusher::add_packet(tlog_block *tb, connection *conn){
	// check if cache exist for this vol
	if (_cache.find(conn->_vol_id_num) == _cache.end()) {
		auto cache = new packet_cache(conn->_vol_id, conn->_vol_id_num);
		_cache[conn->_vol_id_num] = cache;
	}

	_cache[conn->_vol_id_num]->add(tb);
}

void Flusher::init_redis_conn(int idx, int retry_quota) {
	auto ipaddr = make_ipv4_address(ipv4_addr(_objstor_addr,_objstor_port + idx));

	_redis_conns[idx] = new redis_conn(ipaddr, idx);
}

void Flusher::init_redis_conns() {
	auto num = 1 + _k + _m;
		
	for(int i=0; i < num; i++) {
		init_redis_conn(i, 2);
	}
}


/**
 * check if need and able to flush to certain volume.
 * call the flush if needed.
*/
future<flush_result*> Flusher::check_do_flush(uint32_t vol_id_num) {
	flush_result *fr = new flush_result(FLUSH_NO);

	auto cache = _cache[vol_id_num];

	std::queue<tlog_block *> *flush_q = new std::queue<tlog_block*>();
	if (cache->pick(flush_q, _flush_size) == false) {
		delete flush_q;
		return make_ready_future<flush_result*>(fr);
	}
	return flush(vol_id_num, flush_q).finally([flush_q] {
			delete flush_q;
			});
}

// check if we can do periodic flush on this core
future<> Flusher::periodic_flush() {
	std::vector<uint32_t> vols;
	for (auto const &it: _cache) {
		vols.push_back(it.first);
	}
	return do_for_each(vols, [this] (uint32_t vol_id) {
		// if needed, initialize last_flush_time on this vol id
		auto diff = difftime(time(0), _last_flush_time[vol_id]);
		if (diff < _flush_timeout) {
			return make_ready_future<>();
		}

		auto p_cache = _cache[vol_id];
		std::queue<tlog_block *> *flush_q = new std::queue<tlog_block *>();
		if (p_cache->pick(flush_q, p_cache->size()) == false) {
			delete flush_q;
			return make_ready_future<>();
		}
		return flush(vol_id, flush_q).then([flush_q] (auto result) {
				std::cout << "periodic flush at core:" << engine().cpu_id() << "\n";
				return make_ready_future<>();
				}).finally([flush_q] {
					delete flush_q;
				});
	}).then([] {
			return make_ready_future<>();
		});
}

Flusher* get_flusher(shard_id id) {
	return _flushers[id];
}

static size_t capnp_outbuf_size(TlogAggregation::Builder& agg, size_t block_size) {
	return (agg.getSize() * (block_size + 50 /* capnp overhead */)) + CAPNP_OUTBUF_EXTRA;
}

// free all objects created during flush process
static void free_flush_object(unsigned char *encrypted, unsigned char **er_input, unsigned char **er_coding,
		int k, int m) {
	free(encrypted);

	free(er_input[k-1]); // only last element that was allocated
	free(er_input);

	for(int i=0; i < m; i++) {
		free(er_coding[i]);
	}
	free(er_coding);
}

future<flush_result*> Flusher::flush(uint32_t vol_id, std::queue<tlog_block *>* pq) {
	uint8_t last_hash[HASH_LEN];

	return get_last_hash(vol_id, last_hash).then([this, vol_id, pq, last_hash = std::move(last_hash)] (auto ok) {
			if (!ok) {
				auto *fr = new flush_result(FLUSH_MAX_TLOGS_FAILED);
				return make_ready_future<flush_result*>(fr);
			}
			return this->do_flush(vol_id, pq, std::move(last_hash));
		});
}

/* flush the packets to it's storage */
future<flush_result*> Flusher::do_flush(uint32_t volID, std::queue<tlog_block *>* pq, uint8_t *last_hash) {
	flush_count++;
	std::cout << "[flush]vol:" << volID <<".count:"<< flush_count;
	std::cout << ".size:" << pq->size() << ".core:" << engine().cpu_id() << "\n";

	auto p_cache = _cache[volID];
	// remember last time we flush
	_last_flush_time[volID] = time(0);

	/************************** create capnp aggregation object ***************************/
	::capnp::MallocMessageBuilder msg;
	auto agg = msg.initRoot<TlogAggregation>();
	
	agg.setSize(pq->size());
	agg.setName("my aggregation v6");
	agg.setPrev(kj::arrayPtr(last_hash, HASH_LEN));
	
	// build the aggregation blocks
	auto blocks = agg.initBlocks(pq->size());
	size_t block_size = 0;

	std::vector<uint64_t> sequences;
	for (int i=0; !pq->empty(); i++) {
		auto block = blocks[i];
		tlog_block *tb = pq->front();
		if (block_size == 0) {
			block_size = tb->length();
		}
		pq->pop();
		encodeBlock(tb, &block, p_cache->_vol_id);
		sequences.push_back(block.getSequence());
		delete tb;
	}

	// encode it
	kj::byte outbuf[capnp_outbuf_size(agg, block_size)];
	kj::ArrayOutputStream aos(kj::arrayPtr(outbuf, sizeof(outbuf)));
	writeMessage(aos, msg);
	kj::ArrayPtr<kj::byte> bs = aos.getArray();

	/************************* compress ******************************************/
	std::string compressed;
	snappy::Compress((const char *) bs.begin(), bs.size(), &compressed);

	/*********************** encrypt *********************************************/
	// encrypt
	// - align the compressed to 16 bytes boundary
	// - allocate the output (same size)
	compressed.resize(compressed.length() + 16 - (compressed.length() % 16));

	unsigned char *encrypted = (unsigned char *) malloc (sizeof(unsigned char) * compressed.length());
	int enc_len = aes_cbc_enc_256((void *) compressed.c_str(), _enc_iv, _enc_key, encrypted, compressed.length());

	
	/************************* erasure encoding *************************************/
	int chunksize = _er->chunksize(enc_len);

	// build the inputs for erasure coding
	unsigned char **inputs = (unsigned char **) malloc(sizeof(unsigned char *) * _k);
	for (int i=0; i < _k; i++) {
		int to_copy = i == _k -1 ? enc_len - (chunksize * (_k-1)) : chunksize;
		if (i < _k-1) {
			// we can simply use pointer
			inputs[i] =  (unsigned char *) encrypted + (chunksize * i);
		} else {
			// we need to do memcpy here because
			// we might need to add padding to last part
			inputs[i] = (unsigned char*) malloc(sizeof(char) * chunksize);
			memcpy(inputs[i], encrypted + (chunksize * i), to_copy);
		}
	}
	
	// allocate coding result
	unsigned char **coding = (unsigned char **) malloc(sizeof(unsigned char *) * _m);
	for (int i=0; i < _m; i++) {
		coding[i] = (unsigned char*) malloc(sizeof(char) * chunksize);
	}
	_er->encode(inputs, coding, chunksize);

	/*************************** hash the encrypted object **********************/
	uint8_t new_hash[HASH_LEN];
	if (hash_gen(new_hash, (uint8_t *) encrypted, enc_len,
			last_hash, HASH_LEN) != 0) {
		
		free_flush_object(encrypted, inputs, coding, _k, _m);
		
		auto *fr = new flush_result(FLUSH_MAX_TLOGS_FAILED);
		return make_ready_future<flush_result*>(fr);
	}
	
	/***************************** store erasure encoded data **********************/
	return storeEncodedAgg(volID, (const char *)new_hash, HASH_LEN, (const char **)inputs, 
			(const char **)coding,chunksize).then([this, inputs, coding, sequences, encrypted] (auto ok){
				
						free_flush_object(encrypted, inputs, coding, _k, _m);
						
						flush_result *fr = new flush_result(ok? FLUSH_MAX_TLOGS_OK : FLUSH_MAX_TLOGS_FAILED);
						fr->sequences = sequences;
						
						return make_ready_future<flush_result*>(fr);
				});
}
	

future<bool> Flusher::storeEncodedAgg(uint64_t vol_id, const char *hash, int hash_len,
		const char **data, const char **coding, int chunksize) {

	semaphore *finished = new semaphore(0);
	std::vector<bool> *ok_vec = new std::vector<bool>(_k + _m + 1);
	std::vector<bool> &vr = *ok_vec;

	for (int i=0; i < _k; i++) {
		auto rc = _redis_conns[i+1];
		rc->set(hash, hash_len, data[i], chunksize).then([this, finished,i,&vr] (auto ok){
				vr[i+1] = ok;
				}).finally([finished] {
					finished->signal();
				});
	}

	// store the coded data
	for (int i=0; i < _m; i++) {
		auto rc = _redis_conns[i + 1 + _k];
		rc->set(hash, hash_len, coding[i], chunksize).then([this, finished, i, &vr] (auto ok){
				vr[i + 1 + _k] = ok;
			}).finally([finished] {
				finished->signal();
			});
	}

	// store the hash in both memory and redis
	auto p_cache = _cache[vol_id];
	std::string last = "last_hash_" + p_cache->_vol_id;
	
	auto rc = _redis_conns[0];
	
	rc->set(last.c_str(), last.length(), hash, hash_len).then([this, finished, &vr] (auto ok){
				vr[0] = ok;
			}).finally([finished] {
				finished->signal();
			});

	auto old_last = g_last_hash[vol_id];
	uint8_t *new_hash = (uint8_t *) malloc(hash_len);
	
	std::memcpy(new_hash, hash, hash_len);

	if (old_last != NULL) {
		free(old_last);
	}

	g_last_hash[vol_id] = new_hash;
	return finished->wait(_k + _m + 1).then([this, finished, &vr] {
			delete finished;
			auto is_ok = true;
			for (unsigned i=0; i < vr.size(); i++) {
				if (!vr[i]) {
					is_ok = false;
				}
			}
			return make_ready_future<bool>(is_ok);
			});
}


	
void Flusher::encodeBlock(tlog_block *tb, TlogBlock::Builder* builder, std::string& vol_id) {
		builder->setVolumeId(vol_id);
		builder->setSequence(tb->_sequence);
		builder->setLba(tb->_lba);
		builder->setSize(tb->_size);
		builder->setCrc32(tb->_crc);
		builder->setData(capnp::Data::Reader(tb->_data, tb->_size));
		builder->setTimestamp(tb->_timestamp);
	}

int Flusher::hash_gen(uint8_t *new_hash, const uint8_t *data, uint8_t data_len,
			const uint8_t *key, int key_len) {
		
	return blake2bp(new_hash, data, key, HASH_LEN, data_len, key_len);
}


future<bool> Flusher::get_last_hash(uint32_t vol_id, uint8_t *hash) {
	// get last hash from memory
	auto last_hash = g_last_hash[vol_id];
	if (last_hash != NULL) {
		memcpy(hash, last_hash, HASH_LEN);
		return make_ready_future<bool>(true);
	}

	// get last hash from redis
	auto p_cache = _cache[vol_id];

	auto rc = _redis_conns[0];
	auto key = std::string("last_hash_" +  p_cache->_vol_id);
	return rc->get(key, hash, HASH_LEN).then([this, hash] (auto ok) {
			if (!ok) { // if not exist in redis, use private key
				memcpy(hash, _priv_key.c_str(), HASH_LEN);
			}
			return make_ready_future<bool>(true);
		});
}

