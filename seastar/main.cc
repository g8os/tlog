#include "core/app-template.hh"
#include "core/future-util.hh"
#include "core/timer-set.hh"
#include "core/shared_ptr.hh"
#include "core/units.hh"
#include "core/distributed.hh"
#include "core/bitops.hh"
#include "core/sleep.hh"

#include "tlog_schema.capnp.h"
#include <capnp/message.h>
#include <kj/io.h>
#include <kj/common.h>
#include <capnp/serialize-packed.h>
#include <iostream>
#include <vector>
#include <queue>
#include "core/queue.hh"
#include "core/semaphore.hh"
#include <hiredis/hiredis.h>
#include <blake2.h>
#include <isa-l/erasure_code.h>
#include <assert.h>

#define PLATFORM "seastar"
#define VERSION "v1.0"
#define VERSION_STRING PLATFORM " " VERSION

future<> slow() {
    return sleep(std::chrono::seconds(10));
}
namespace tlog {

using clock_type = lowres_clock;

static std::queue<uint8_t *> gPackets;
static	semaphore gSem{1};

const int buf_size = 4144; /* size of the message we receive from client */
const int FLUSH_SIZE = 10;
const int K = 4;
const int M = 2;

struct system_stats {
    uint32_t _curr_connections {};
    uint32_t _total_connections {};
    uint64_t _cmd_get {};
    uint64_t _cmd_set {};
    uint64_t _cmd_flush {};
    clock_type::time_point _start_time;
public:
    system_stats() {
        _start_time = clock_type::time_point::max();
    }
    system_stats(clock_type::time_point start_time)
        : _start_time(start_time) {
    }
    void operator+=(const system_stats& other) {
        _curr_connections += other._curr_connections;
        _total_connections += other._total_connections;
        _cmd_get += other._cmd_get;
        _cmd_set += other._cmd_set;
        _cmd_flush += other._cmd_flush;
        _start_time = std::min(_start_time, other._start_time);
    }
    future<> stop() { return make_ready_future<>(); }
	

};

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

class tcp_server {
private:
    lw_shared_ptr<server_socket> _listener;
    distributed<system_stats>& _system_stats;
	std::string _objstor_addr;
	int _objstor_port;
	std::string _priv_key;
    uint16_t _port;
    struct connection {
        connected_socket _socket;
        socket_address _addr;
        input_stream<char> _in;
        output_stream<char> _out;
        distributed<system_stats>& _system_stats;
        connection(connected_socket&& socket, socket_address addr, distributed<system_stats>& system_stats)
            : _socket(std::move(socket))
            , _addr(addr)
            , _in(_socket.input())
            , _out(_socket.output())
            , _system_stats(system_stats)
        {
            _system_stats.local()._curr_connections++;
            _system_stats.local()._total_connections++;
        }
        ~connection() {
            _system_stats.local()._curr_connections--;
        }
    };
public:
    tcp_server(distributed<system_stats>& system_stats, int objstor_port,
			uint16_t port = 11211)
        : _system_stats(system_stats)
		//, _objstor_addr(objstor_addr)
		, _objstor_port(objstor_port)
		//, _priv_key(priv_key)
        , _port(port)
    {
		_objstor_addr = "192.168.0.100";
		_objstor_port = 16379;
		_priv_key = "my-secret-key";
		std::cout << "start tlog with port = " << _port << ". objstor port = " << _objstor_port << "\n";
	}

    void start() {
        listen_options lo;
        lo.reuse_address = true;
        _listener = engine().listen(make_ipv4_address({_port}), lo);
        keep_doing([this] {
            return _listener->accept().then([this] (connected_socket fd, socket_address addr) mutable {
                auto conn = make_lw_shared<connection>(std::move(fd), addr, _system_stats);
                do_until([conn] { return conn->_in.eof(); }, [this, conn] {
                    return this->myhandle(conn->_in, conn->_out).then([conn] {
                        return conn->_out.flush();
                    });
                }).finally([conn] {
                    return conn->_out.close().finally([conn]{});
                });
            });
        }).or_terminate();
    }

    future<> stop() { return make_ready_future<>(); }
	
	future<> myhandle(input_stream<char>& in, output_stream<char>& out) {
    	return repeat([this, &out, &in] {
        	return in.read_exactly(buf_size).then( [this, &out] (temporary_buffer<char> buf) {
            	if (buf) {
					uint8_t *packet = (uint8_t *) malloc(buf.size());
					memcpy(packet, buf.get(), buf.size()); // TODO : find a way to avoid copying
					this->addPacket(packet);
                	return make_ready_future<stop_iteration>(stop_iteration::no);
            	} else {
                	return make_ready_future<stop_iteration>(stop_iteration::yes);
            	}
        	});
    	}).then([&out] {
        	return make_ready_future<>();
    	});

	}
	void addPacket(uint8_t *packet) {
		gSem.wait();
		gPackets.push(packet);
		std::cout << "-> packets size= " << gPackets.size() << "\n";
		if (gPackets.size() >= FLUSH_SIZE) {
			this->flush();
		}
		gSem.signal();
	}

private:
	void flush() {
		::capnp::MallocMessageBuilder msg;
		auto agg = msg.initRoot<TlogAggregation>();
		
		agg.setSize(gPackets.size());
		agg.setName("my aggregation v5");

		// build the blocks
		auto blocks = agg.initBlocks(gPackets.size());
		for (int i =0; !gPackets.empty(); i++) {
			auto encoded = gPackets.front();
			gPackets.pop();

			auto blockReader = this->decodeBlock(encoded, buf_size);
			auto blockBuilder = blocks[i];
			this->encodeBlock(blockReader, &blockBuilder);
			free(encoded);
		}

	
		// encode it again
		int outbufSize = (agg.getSize() * buf_size) + 100;
		kj::byte outbuf[outbufSize];
		kj::ArrayOutputStream aos(kj::arrayPtr(outbuf, sizeof(outbuf)));
		writeMessage(aos, msg);
		
		kj::ArrayPtr<kj::byte> bs = aos.getArray();
		std::cout << "encoded msg len = " << bs.size() << ". outbuf size = " << outbufSize << "\n";

		/** erasure coded it and send the pieces to separate ardb server **/
		int k = K;
		int m = M;
		Erasurer er(k, m);
		int chunksize = er.chunksize(bs.size());
		
		unsigned char **inputs = (unsigned char **) malloc(sizeof(unsigned char *) * k);
		for (int i=0; i < k; i++) {
			int to_copy = i == k -1 ? bs.size() - (chunksize * (k-1)) : chunksize;
			// TODO we might be able to avoid malloc and memcpy here
			// except for the last piece
			inputs[i] = (unsigned char*) malloc(sizeof(char) * chunksize);
			memcpy(inputs[i], bs.begin() + (chunksize * i), to_copy);
		}

		// allocate coding result
		unsigned char **coding = (unsigned char **) malloc(sizeof(unsigned char *) * m);
		for (int i=0; i < m; i++) {
			coding[i] = (unsigned char*) malloc(sizeof(char) * chunksize);
		}

		er.encode(inputs, coding, chunksize);

		int hash_len = 32;
		uint8_t *hash = hash_gen(bs.begin(), bs.size(), hash_len);
		storeEncodedAgg(hash, hash_len, inputs, coding, k, m, chunksize);
		free(hash);
	}

	/**
	 * store erasure encoded data to redis-compatible server
	 */
	void storeEncodedAgg(uint8_t *hash, int hash_len, 
			unsigned char **data, unsigned char **coding, int k, int m, int chunksize) {
		// TODO make it async
		// store the data
		for (int i=0; i < k; i++) {
			store(_objstor_addr, _objstor_port + i + 1, hash, hash_len, data[i], chunksize);
		}
		// store the coded data
		for (int i=0; i < m; i++) {
			store(_objstor_addr, _objstor_port + k + i + 1, hash, hash_len, coding[i], chunksize);
		}
		std::string last = "last_encoded";
		store(_objstor_addr, _objstor_port, (uint8_t *) last.c_str(), last.length(), (unsigned char *) hash, hash_len);
	}


	/**
	 * store data to redis compatible server
	 */
	void store(std::string redis_addr, int redis_port, uint8_t *key, int key_len, unsigned char *data, int data_len) {
		redisContext *c;
		redisReply *reply;

		// TODO : make persistent connection to redis
		c = redisConnect(redis_addr.c_str(), redis_port);
		reply = (redisReply *)redisCommand(c, "SET %b %b", key, key_len, data, data_len);

		// TODO : check reply and raise exception in case of error
		std::cout << "store " << data_len << " bytes to redis " << redis_port <<" \n";
		std::cout << "redis reply :" << reply->str << "\n";

		freeReplyObject(reply);
		redisFree(c);
	}

	uint8_t* hash_gen(uint8_t *data, uint8_t data_len, int hash_len) {
		uint8_t *hash = (uint8_t *) malloc(sizeof(uint8_t) * hash_len);
		if (blake2bp(hash, data, _priv_key.c_str(), hash_len, data_len, _priv_key.length()) != 0) {
			std::cerr << "failed to hash\n";
			exit(1); // TODO : better error handling
		}
		return hash;
	}

	TlogBlock::Reader decodeBlock(uint8_t *encoded, int len) {
		auto apt = kj::ArrayPtr<kj::byte>(encoded, len);
		kj::ArrayInputStream ais(apt);
		::capnp::MallocMessageBuilder message;
		readMessageCopy(ais, message);
		return message.getRoot<TlogBlock>();
	}

	void encodeBlock(TlogBlock::Reader reader, TlogBlock::Builder* builder) {
		builder->setSequence(reader.getSequence());
		builder->setSize(reader.getSize());
		builder->setCrc(reader.getCrc());
		builder->setData(reader.getData());
	}
};


} /* namespace tlog */

int main(int ac, char** av) {
    distributed<tlog::system_stats> system_stats;
    distributed<tlog::tcp_server> tcp_server;

    app_template app;
    return app.run_deprecated(ac, av, [&] {
        engine().at_exit([&] { return tcp_server.stop(); });
        engine().at_exit([&] { return system_stats.stop(); });

        auto&& config = app.configuration();
		
		// TODO : make thse configs configurable
        uint16_t port = 11211;
		//std::string objstor_addr = "192.168.0.100";
		int objstor_port = 16379;
		//std::string priv_key = "my-secret-key";

        return system_stats.start(tlog::clock_type::now()).then([&] {
            std::cout << PLATFORM << " tlog " << VERSION << "\n";
            return make_ready_future<>();
        }).then([&, port] {
            return tcp_server.start(std::ref(system_stats), objstor_port, port);
        }).then([&tcp_server] {
            return tcp_server.invoke_on_all(&tlog::tcp_server::start);
        }).then([start_stats = config.count("stats")] {
            // what we really wanted to do here is to
            // avoid the server to exit
            // need a better way here than sleeping
            return repeat([] {
                return slow().then([] { return stop_iteration::no; });
            });
        });
    });
}