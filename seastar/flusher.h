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
#include <hiredis/hiredis.h>

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

	/* connection to redis meta server */
	redisContext* _meta_redis_conn;

	/* connection to storage servers */
	std::vector<redis_conn *> _redis_conns;

	/* packets cache by volume id*/
	std::map<uint32_t, std::map<uint64_t, uint8_t *>> _packets;

	/* last time we do flushing per volume id*/
	std::map<uint32_t, time_t> _last_flush_time;
public:
	Flusher() {
	}
	Flusher(std::string objstor_addr, int objstor_port, std::string priv_key, 
			int flush_size, int flush_timeout, int k, int m);
	void add_packet(uint8_t *packet, uint32_t vol_id, uint64_t seq);

	future<flush_result> check_do_flush(uint32_t vol_id);
	
	future<> periodic_flush();

	void post_init();

private:
	future<> init_redis_conn(int idx);
	void init_redis_conns();
	
	void create_meta_redis_conn();

	bool pick_to_flush(uint64_t vol_id, std::queue<uint8_t *> *q, int flush_size);

	future<flush_result> flush(uint32_t volID, std::queue<uint8_t *> pq);

	bool ok_to_flush(uint32_t vol_id, int flush_size);

	future<bool> storeEncodedAgg(uint64_t vol_id, uint8_t *hash, int hash_len,
			unsigned char **data, unsigned char **coding, int chunksize);

	uint8_t* hash_gen(uint64_t vol_id, uint8_t *data, uint8_t data_len,
			uint8_t *key, int key_len);

	void get_last_hash(uint32_t volID, uint8_t *hash, int *hash_len, bool retried = false);

	void encodeBlock(uint8_t *encoded, int len, TlogBlock::Builder* builder);
};

Flusher* get_flusher(shard_id id);
#endif
