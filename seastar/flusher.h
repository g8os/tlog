#ifndef _FLUSHER_H
#define _FLUSHER_H
#include "core/seastar.hh"
#include "core/reactor.hh"
#include "core/future-util.hh"
#include "core/app-template.hh"

#include <ctime>

// capnp
#include "tlog_schema.capnp.h"
#include <capnp/message.h>
#include <kj/io.h>
#include <kj/common.h>
#include <capnp/serialize-packed.h>


#include <isa-l/erasure_code.h>
#include <blake2.h>

#include "packet_cache.h"
#include "tlog_block.h"
#include "redis_conn.h"

/* Erasure encoder */
class Erasurer {
private:
	int _k;
	int _m;
	unsigned char* _encode_matrix;
	unsigned char* _encode_tab;
public:
	Erasurer(int k, int m)
		: _k(k)
		, _m(m)
	{
		_m = m;
		_encode_matrix = (unsigned char *) malloc(sizeof(char) * _k * (_k + _m));
		_encode_tab = (unsigned char *) malloc(sizeof(char) * 32 * k  * ( _k + _m));
		gf_gen_cauchy1_matrix(_encode_matrix, _k+_m, _k);
		ec_init_tables(_k, _m, &_encode_matrix[_k * _k], _encode_tab);
	}
	~Erasurer() {
		free(_encode_matrix);
		free(_encode_tab);
	}


	void encode(unsigned char **data, unsigned char **coding, int chunksize) {
		ec_encode_data(chunksize, _k, _m, _encode_tab, data, coding);
	}

	/**
	 * Count chunk size given the data_len.
	 * TODO : check again if we do it in the right way.
	 */
	int chunksize(int data_len) {
		int size = data_len / _k;
		if (data_len % _k > 0) {
			size++;
		}
		return size;
	}
};

enum tlog_status {
	TLOG_MSG_CORRUPT = -4,			// tlog message corrupt (by crc check)
	FLUSH_TIMEOUT_FAILED = -3,
	FLUSH_MAX_TLOGS_FAILED = -2,	// flush (because of max tlogs) failed
	FLUSH_TLOG_FAILED = -1,			// tlog message failed to be received
	FLUSH_NO = 0, 					// tlog message received, but no flush
	FLUSH_MAX_TLOGS_OK = 1,			// flush (because of max tlogs) OK
	FLUSH_TIMEOUT_OK = 2			// flush (because of timeout) OK
};

struct flush_result {
	int status;
	std::vector<uint64_t> sequences;
public:
	flush_result(int new_status) {
		status = new_status;
	}
	~flush_result() {
		sequences.resize(0);
	}

	/** approximate size of this object */
	int approx_size() {
		return sizeof(int) + (sizeof(uint64_t) * sequences.size());
	}
};

class Flusher {
private:
	/* min number of packets before being flushed */
	int _flush_size;

	/* min number of seconds before packets being flushed
	 * without waiting it to reach _flush_size
	 */
	int _flush_timeout;

	/* K value of erasure encoding */
	int _k;

	/* M value of erasure encoding */
	int _m;

	/* objstor address and port */
	std::string _objstor_addr;
	int _objstor_port;

	/* private key */
	std::string _priv_key;

	/* connection to storage servers */
	std::vector<redis_conn *> _redis_conns;

	/* packets cache by volume id*/
	//std::map<uint32_t, std::map<uint64_t, tlog_block *>> _packets;
	
	std::map<uint32_t, packet_cache *> _cache;

	/* last time we do flushing per volume id*/
	std::map<uint32_t, time_t> _last_flush_time;

	/* encryption input vector */
	uint8_t _enc_iv[16];

	/* encryption key */
	uint8_t _enc_key[256];
public:
	Flusher() {
	}
	Flusher(std::string objstor_addr, int objstor_port, std::string priv_key, 
			int flush_size, int flush_timeout, int k, int m);

	void add_packet(tlog_block *tb);

	future<flush_result*> check_do_flush(uint32_t vol_id);
	
	future<> periodic_flush();

	void post_init();

private:
	void init_redis_conn(int idx, int retry_quota=0);
	void init_redis_conns();

	future<flush_result*> flush(uint32_t volID, std::queue<tlog_block *>* pq);
	future<flush_result*> do_flush(uint32_t volID, std::queue<tlog_block *>* pq, uint8_t *last_hash);

	future<bool> storeEncodedAgg(uint64_t vol_id, const char *hash, int hash_len,
			const char **data, const char **coding, int chunksize);

	int hash_gen(uint8_t *new_hash, const uint8_t *data, uint8_t data_len,
			const uint8_t *key, int key_len);

	future<bool> get_last_hash(uint32_t vol_id, uint8_t *hash);

	void encodeBlock(tlog_block *tb, TlogBlock::Builder* builder);
};

Flusher* get_flusher(shard_id id);
#endif
