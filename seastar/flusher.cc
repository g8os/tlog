#include "flusher.h"

#include "core/sleep.hh"

// capnp
#include "tlog_schema.capnp.h"
#include <capnp/message.h>
#include <kj/io.h>
#include <kj/common.h>
#include <capnp/serialize-packed.h>

// other lib
#include <hiredis/hiredis.h>
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
static const int HASH_LEN = 32;

static const int BUF_SIZE = 16472; /* size of the message we receive from client */

/* number of extra bytes for capnp aggregation encoding
 * TODO : find the correct number. It currently based only
 * on my own guest.
 * */
static const int CAPNP_OUTBUF_EXTRA = 300;

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
	_redis_conns.reserve(_k + _m + 1);
	_redis_conns.resize(_k + _m + 1);
	std::memset(_enc_iv, '0', 16);
	uint8_t dec_key[256];
	aes_keyexp_256((uint8_t *)priv_key.c_str(), _enc_key, dec_key);
	
	create_meta_redis_conn();
}

/**
 * post_init contains steps that (for unknown reason)
 * can't be put inside the class constructor
 */
void Flusher::post_init() {
	_flushers[engine().cpu_id()] = this;
	init_redis_conns();
}

void Flusher::add_packet(uint8_t *packet, uint32_t vol_id, uint64_t seq){
	this->_packets[vol_id][seq] = packet;
}

future<> Flusher::init_redis_conn(int idx) {
	auto ipaddr = make_ipv4_address(ipv4_addr(_objstor_addr,_objstor_port + idx));
	
	return connect(ipaddr).then([this, idx] (connected_socket s) {
			auto conn = new redis_conn(std::move(s));
			_redis_conns[idx] = std::move(conn);
			return make_ready_future<>();
		});
}

void Flusher::init_redis_conns() {
	auto num = 1 + _k + _m;
		
	for(int i=0; i < num; i++) {
		init_redis_conn(i);
	}
}

void Flusher::create_meta_redis_conn() {
	auto port = _objstor_port;

	redisContext *c = redisConnect(_objstor_addr.c_str(), port);
	if (c == NULL || c->err || redisEnableKeepAlive(c) != REDIS_OK) {
		exit(-1); // TODO raise exception
	}
	_meta_redis_conn = c;
}


/**
 * check if need and able to flush to certain volume.
 * call the flush if needed.
*/
future<flush_result> Flusher::check_do_flush(uint32_t vol_id) {
	std::queue<uint8_t *> flush_q;
	flush_result fr;
	fr.status = FLUSH_NO;
	
	if (!ok_to_flush(vol_id, _flush_size)) {
		return make_ready_future<flush_result>(fr);
	}
	if (_packets[vol_id].size() < (unsigned)_flush_size) {
		return make_ready_future<flush_result>(fr);
	}
	if (pick_to_flush(vol_id, &flush_q, _flush_size) == false) {
		return make_ready_future<flush_result>(fr);
	}
	return flush(vol_id, flush_q);
}

// check if we can do periodic flush on this core
future<> Flusher::periodic_flush() {
	std::vector<uint32_t> vols;
	for (auto const &it: _packets) {
		vols.push_back(it.first);
	}
	return do_for_each(vols, [this] (uint32_t vol_id) {
		// if needed, initialize last_flush_time on this vol id
		if (_last_flush_time.find(vol_id) == _last_flush_time.end()) {
			_last_flush_time[vol_id] = time(0);
		}

		if (_packets[vol_id].size() == 0) {
			return make_ready_future<>();
		}

		auto diff = difftime(time(0), _last_flush_time[vol_id]);
		if (diff < _flush_timeout) {
			return make_ready_future<>();
		}

		if (!ok_to_flush(vol_id, _packets[vol_id].size())) {
			return make_ready_future<>();
		}

		std::queue<uint8_t *> flush_q;
		if (pick_to_flush(vol_id, &flush_q, _packets[vol_id].size()) == false) {
			return make_ready_future<>();
		}
		return flush(vol_id, flush_q).then([] (auto result) {
				std::cout << "periodic flush at core:" << engine().cpu_id() << "\n";
				return make_ready_future<>();
				});
	}).then([] {
			return make_ready_future<>();
            //return sleep(std::chrono::seconds(2));
		});
}

/**
 * pick packets to be flushed.
 * It must be called under flush & sem lock
*/

bool Flusher::pick_to_flush(uint64_t vol_id, std::queue<uint8_t *> *q, int flush_size) {
	auto it = _packets[vol_id].begin();
	for (int i=0; i  < flush_size && it != _packets[vol_id].end(); i++) {
		q->push(it->second);
		it = _packets[vol_id].erase(it);
	}
	return true;
}

Flusher* get_flusher(shard_id id) {
	return _flushers[id];
}

/* flush the packets to it's storage */
future<flush_result> Flusher::flush(uint32_t volID, std::queue<uint8_t *> pq) {
	flush_count++;
	std::cout << "[flush] vol:" << volID <<".count:"<< flush_count;
	std::cout << ".flush_size:" << pq.size() << ".at core:" << engine().cpu_id() << "\n";

	_last_flush_time[volID] = time(0);

	uint8_t last_hash[HASH_LEN];
	int last_hash_len;
	
	get_last_hash(volID, last_hash, &last_hash_len);

	// create aggregation object
	::capnp::MallocMessageBuilder msg;
	auto agg = msg.initRoot<TlogAggregation>();
	
	agg.setSize(pq.size());
	agg.setName("my aggregation v6");
	agg.setPrev(kj::arrayPtr(last_hash, HASH_LEN));
	
	// build the aggregation blocks
	// check if we can use packet's memory, to avoid memcpy
	auto blocks = agg.initBlocks(pq.size());

	// TODO : find a way to reuse packet data to avoid
	// memcpy
	std::vector<uint64_t> sequences;
	for (int i=0; !pq.empty(); i++) {
		auto block = blocks[i];
		auto packet = pq.front();
		pq.pop();
		encodeBlock(packet, BUF_SIZE, &block);
		sequences.push_back(block.getSequence());
		free(packet);
	}

	// encode it
	int outbufSize = (agg.getSize() * BUF_SIZE) + CAPNP_OUTBUF_EXTRA;
	kj::byte outbuf[outbufSize];
	kj::ArrayOutputStream aos(kj::arrayPtr(outbuf, sizeof(outbuf)));
	writeMessage(aos, msg);
	kj::ArrayPtr<kj::byte> bs = aos.getArray();

	// compress it
	std::string compressed;
	snappy::Compress((const char *) bs.begin(), bs.size(), &compressed);

	// encrypt
	// - align the compressed to 16 bytes boundary
	// - allocate the output (same size?)
	compressed.resize(compressed.length() + 16 - (compressed.length() % 16));

	unsigned char *encrypted = (unsigned char *) malloc (sizeof(unsigned char) * compressed.length());
	int enc_len = aes_cbc_enc_256((void *) compressed.c_str(), _enc_iv, _enc_key, encrypted, compressed.length());

	
	// erasure encoding
	Erasurer er(_k, _m); // TODO : check if we can reuse this object
	int chunksize = er.chunksize(enc_len);

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
	er.encode(inputs, coding, chunksize);
	
	int hash_len = 32;
	
	//uint8_t hash[bs.size()];
	uint8_t *hash = hash_gen(volID, (uint8_t *) encrypted, enc_len,
			last_hash, last_hash_len);
		
	return storeEncodedAgg(volID, hash, hash_len, inputs, coding,
					chunksize).then([this, hash, inputs, coding, sequences, encrypted] (auto ok){
				
						free(hash);
						free(encrypted);
						// cleanup erasure coding data
						free(inputs[_k-1]);
						free(inputs);
						for(int i=0; i < _m; i++) {
							free(coding[i]);
						}
						flush_result fr;
						fr.status = ok? FLUSH_MAX_TLOGS_OK : FLUSH_MAX_TLOGS_FAILED;
						fr.sequences = sequences;
						free(coding);
						return make_ready_future<flush_result>(fr);
				});
}
	
/**
 * check if it is ok to flush to storage
 * 1. first tlog is one after last flush
 * 2. we have all sequences neded
 * 3. we need to handle if sequence number reset to 0
*/
bool Flusher::ok_to_flush(uint32_t vol_id, int flush_size) {
	auto packetsMap = _packets[vol_id];
	auto it = packetsMap.begin();
	auto prev = it->first;
	++it;
	auto i=1;
	while (it != packetsMap.end() && i < flush_size) {
		if (it->first != prev + 1) {
			return false;
		}
		prev = it->first;
		++it;
		++i;
	}
	return true;
}
	

future<bool> Flusher::storeEncodedAgg(uint64_t vol_id, uint8_t *hash, int hash_len,
		unsigned char **data, unsigned char **coding, int chunksize) {

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
	std::string last = "last_hash_" + std::to_string(vol_id);
	
	auto rc = _redis_conns[0];
	
	rc->set((uint8_t *) last.c_str(), last.length(), 
			(unsigned char *) hash, hash_len).then([this, finished, &vr] (auto ok){
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
					// sleep for a while before re-creating the connection
            		sleep(std::chrono::seconds(5)).then([this, i] {
							init_redis_conn(i);
						});
				}
			}
			return make_ready_future<bool>(is_ok);
			});
}


	
void Flusher::encodeBlock(uint8_t *encoded, int len, TlogBlock::Builder* builder) {
		auto apt = kj::ArrayPtr<kj::byte>(encoded, len);
		kj::ArrayInputStream ais(apt);
		::capnp::MallocMessageBuilder message;
		readMessageCopy(ais, message);
		auto reader = message.getRoot<TlogBlock>();

		builder->setVolumeId(reader.getVolumeId());
		builder->setSequence(reader.getSequence());
		builder->setSize(reader.getSize());
		builder->setCrc32(reader.getCrc32());
		builder->setData(reader.getData());
	}

uint8_t* Flusher::hash_gen(uint64_t vol_id, uint8_t *data, uint8_t data_len,
			uint8_t *key, int key_len) {
		
	uint8_t *hash = (uint8_t *) malloc(sizeof(uint8_t) * HASH_LEN);
	
	if (blake2bp(hash, data, key, HASH_LEN, data_len, key_len) != 0) {
		std::cerr << "failed to hash\n";
		exit(1); // TODO : better error handling
	}
	return hash;
}

/**
 * get hash(1) key.
 * use last hash or priv_key if we dont have last hash.
 * TODO :
 * - avoid memcpy
*/
	
void Flusher::get_last_hash(uint32_t volID, uint8_t *hash, int *hash_len, bool retried) {

	// get last hash from memory
	auto last_hash = g_last_hash[volID];
	if (last_hash != NULL) {
		memcpy(hash, last_hash, HASH_LEN);
		*hash_len = HASH_LEN;
		return;
	}

	
	// get last hash from DB
	redisReply *reply;
	reply = (redisReply *)redisCommand(_meta_redis_conn, "GET last_hash_%u", volID);
	if (reply == NULL) {
		// if we got null, we assume that redis connection is broken
		// we create it again.
		create_meta_redis_conn();
		if (!retried) {
			get_last_hash(volID, hash, hash_len, true);
		}
		return;
	}

	
	// if there is no last hash, use priv key
	if (reply->type == REDIS_REPLY_NIL) {
		std::cout << "use priv_key as last_hash for vo" << volID << "\n";
		memcpy(hash, _priv_key.c_str(), _priv_key.length());
		*hash_len = _priv_key.length();
		
		freeReplyObject(reply);
		return;
	}
	
	// TODO : handle other error types

	memcpy(hash, reply->str, reply->len);
	*hash_len = reply->len;
	freeReplyObject(reply);
}

