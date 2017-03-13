#ifndef _FLUSHER_H
#define _FLUSHER_H
#include "core/seastar.hh"
#include "core/reactor.hh"
#include "core/future-util.hh"
#include "core/app-template.hh"

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


class Flusher {
private:
	int _k;
	int _m;
	std::string _objstor_addr;
	int _objstor_port;
	std::string _priv_key;
	redisContext* _meta_redis_conn;
	std::vector<redis_conn *> _redis_conns;
	std::map<uint32_t, std::map<uint64_t, uint8_t *>> _packets;
	int cpu_id;
public:
	Flusher() {
	}
	Flusher(std::string objstor_addr, int objstor_port, std::string priv_key, int k, int m);
	void reg();
	void init_redis_conns() {
		auto num = 1 + _k + _m;
		for(int i=0; i < num; i++) {
			auto port = _objstor_port + i;
			auto ipaddr = make_ipv4_address(ipv4_addr(_objstor_addr,port));
			connect(ipaddr).then([this, i, port] (connected_socket s) {
				auto conn = new redis_conn(std::move(s));
				_redis_conns[i] = std::move(conn);
			});
		}
	}

	void hello() {
		std::cout << " hello\n";
	}

	redisContext *create_meta_redis_conn() {
		auto port = _objstor_port;

		redisContext *c = redisConnect(_objstor_addr.c_str(), port);

		if (c == NULL || c->err || redisEnableKeepAlive(c) != REDIS_OK) {
			exit(-1); // TODO raise exception
		}
		return c;
	}

	void add_packet(uint8_t *packet, uint32_t vol_id, uint64_t seq);

	future<> check_do_flush(uint32_t vol_id);

	bool pick_to_flush(uint64_t vol_id, std::queue<uint8_t *> *q);

	future<> flush(uint32_t volID, std::queue<uint8_t *> pq);

	bool ok_to_flush(uint32_t volID);

	future<> storeEncodedAgg(uint64_t vol_id, uint8_t *hash, int hash_len,
			unsigned char **data, unsigned char **coding, int chunksize);

	uint8_t* hash_gen(uint64_t vol_id, uint8_t *data, uint8_t data_len,
			uint8_t *key, int key_len);

	void get_last_hash(uint32_t volID, uint8_t *hash, int *hash_len, bool retried = false);

	void encodeBlock(uint8_t *encoded, int len, TlogBlock::Builder* builder);
};

Flusher* get_flusher(shard_id id);
#endif
