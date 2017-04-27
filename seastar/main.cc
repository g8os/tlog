#include "core/app-template.hh"
#include "core/future-util.hh"
#include "core/timer-set.hh"
#include "core/shared_ptr.hh"
#include "core/units.hh"
#include "core/distributed.hh"
#include "core/bitops.hh"
#include "core/sleep.hh"

// C++
#include <iostream>
#include <assert.h>
#include <vector>

#include <isa-l/crc.h>

#include "redis_conn.h"
#include "flusher.h"
#include "tlog_block.h"
#include "connection.h"

#define PLATFORM "seastar"
#define VERSION "v1.0"
#define VERSION_STRING PLATFORM " " VERSION

#include "util/log.hh"
seastar::logger logger("tlog");

future<> periodic_flush() {
        return do_for_each(boost::counting_iterator<int>(0),
                boost::counting_iterator<int>((int)smp::count),
                [] (int i) {
				return smp::submit_to(i, [] {
					auto flusher = get_flusher(engine().cpu_id());
					return flusher->periodic_flush();
				});
        }).then([] {
            // wait one second before starting the next iteration
            return sleep(std::chrono::seconds(1));
        });
}

namespace tlog {

static std::vector<void *> servers;

using clock_type = lowres_clock;

/* erasure encoding variable */
int K = 4;
int M = 2;

/* flush settings */
int FLUSH_TIME;
int FLUSH_SIZE;

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

class tcp_server {
private:
    lw_shared_ptr<server_socket> _listener;
    distributed<system_stats>& _system_stats;
	std::string _objstor_addr;
	int _objstor_port;
	std::string _priv_key;
	Flusher _flusher;

    uint16_t _port;
public:
    tcp_server(distributed<system_stats>& system_stats, std::string objstor_addr,
			int objstor_port, std::string priv_key, uint16_t port = 11211)
        : _system_stats(system_stats)
		, _objstor_addr(objstor_addr)
		, _objstor_port(objstor_port)
		, _priv_key(priv_key)
        , _port(port)
    {
		_flusher = Flusher(_objstor_addr, _objstor_port, _priv_key, 
				FLUSH_SIZE, FLUSH_TIME, K, M);
		
		// TODO : for some unknown reason, it can't be put into 
		// Flusher's constructor
		_flusher.post_init();
		std::cout << "TCP server started at core " << engine().cpu_id() << "\n";
	}

    void start() {
        listen_options lo;
        lo.reuse_address = true;
        _listener = engine().listen(make_ipv4_address({_port}), lo);
        keep_doing([this] {
            return _listener->accept().then([this] (connected_socket fd, socket_address addr) mutable {
                auto conn = new connection(std::move(fd), addr);
                do_until([conn] { return conn->_in.eof(); }, [this, conn] {
                    return this->handle(conn).then([conn] {
                        return conn->_out.flush();
                    });
                }).finally([conn] {
                    return conn->_out.close().finally([conn]{});
					delete conn;
                });
            });
        }).or_terminate();
    }

    future<> stop() { return make_ready_future<>(); }

	/**
	 * handle incoming packet
	 */
	future<> handle(connection *conn) {
    	return repeat([this, conn] {
        	return conn->read().then( [this, conn] (temporary_buffer<char> buf) {
				// Check if we receive data with expected size.
				// Unexpected size indicated broken client/connection,
				// we close it for simplicity.
            	if (buf) {
					return handle_packet(std::move(buf), conn).then([]{
						return make_ready_future<stop_iteration>(stop_iteration::no);
					});
            	} else {
                	return make_ready_future<stop_iteration>(stop_iteration::yes);
            	}
        	});
    	}).then([] {
        	return make_ready_future<>();
    	});

	}

	future<> handle_packet(temporary_buffer<char> buf, connection *conn) {
		// decode the message
		kj::ArrayPtr<const capnp::word> words(
				reinterpret_cast<const capnp::word*>((unsigned char *) buf.begin()),
				buf.size() / sizeof(capnp::word));
		::capnp::FlatArrayMessageReader famr(words);
		TlogBlock::Reader block = famr.getRoot<TlogBlock>();

		conn->set_vol_id(block.getVolumeId().cStr(), block.getVolumeId().size());


		// check crc of the packet
		auto crc32 = crc32_ieee(0, block.getData().begin(), block.getData().size());
		if (crc32 != block.getCrc32()) {
			flush_result *fr = new flush_result(TLOG_MSG_CORRUPT);
			fr->sequences.push_back(block.getSequence());
			return this->send_response(conn->_out, fr);
		}

		auto tb = new tlog_block(conn, &block);

		return smp::submit_to(conn->_vol_id_num % smp::count, [this, tb, conn] {
				auto flusher = get_flusher(engine().cpu_id());
				flusher->add_packet(tb, conn);
				return flusher->check_do_flush(conn->_vol_id_num);
		}).then([this, conn] (auto fr) {
			return this->send_response(conn->_out, fr);
		});
	}

private:
	future<> send_response(output_stream<char>& out, flush_result* fr) {
		// create capnp object
		::capnp::MallocMessageBuilder msg;
		auto agg = msg.initRoot<TlogResponse>();
		
		agg.setStatus(fr->status);
		auto sequences = agg.initSequences(fr->sequences.size());
		for (unsigned i=0; i < fr->sequences.size(); i++) {
			sequences.set(i, fr->sequences[i]);
		}

		// encode capnp object
		kj::byte outbuf[fr->approx_size() + 30];
		kj::ArrayOutputStream aos(kj::arrayPtr(outbuf, sizeof(outbuf)));
		writeMessage(aos, msg);
		kj::ArrayPtr<kj::byte> bs = aos.getArray();

		delete fr;

		// send it
		std::string prefix = std::to_string(bs.size()) + "\r\n";
		auto tbuf = temporary_buffer<char>(prefix.c_str(), prefix.length());
		return out.write(std::move(tbuf)).then([&out, bs] {
			auto tbuf = temporary_buffer<char>((const char *) bs.begin(), bs.size());
			return out.write(std::move(tbuf));
		}).then([&out] {
			return out.flush();
		});
	}
};


} /* namespace tlog */

int main(int ac, char** av) {
    distributed<tlog::system_stats> system_stats;
    distributed<tlog::tcp_server> tcp_server;

	namespace bpo = boost::program_options;
    app_template app;

	app.add_options()
		("port", bpo::value<uint16_t>()->default_value(11211), "tcp port")
		("flush_size", bpo::value<int>()->default_value(25), "flush_size")
		("flush_time", bpo::value<int>()->default_value(25), "flush_time (seconds)")
		("k", bpo::value<int>()->default_value(4), "K variable of erasure encoding")
		("m", bpo::value<int>()->default_value(2), "M variable of erasure encoding")
		("objstor_addr", bpo::value<std::string>()->default_value("127.0.0.1"), "objstor address")
		("objstor_port", bpo::value<int>()->default_value(16379), "objstor first port")
		("priv_key", bpo::value<std::string>()->default_value("12345678901234567890123456789012"), "private key")
		("debug_log", bpo::value<bool>()->default_value(false), "set true to enable debug log")

		;
    return app.run_deprecated(ac, av, [&] {
        engine().at_exit([&] { return tcp_server.stop(); });
        engine().at_exit([&] { return system_stats.stop(); });

        auto&& config = app.configuration();

        uint16_t port = config["port"].as<uint16_t>();
		std::string objstor_addr = config["objstor_addr"].as<std::string>();
		int objstor_port = config["objstor_port"].as<int>();
		std::string priv_key = config["priv_key"].as<std::string>();
		tlog::K = config["k"].as<int>();
		tlog::M = config["m"].as<int>();
		tlog::FLUSH_SIZE = config["flush_size"].as<int>();
		tlog::FLUSH_TIME = config["flush_time"].as<int>();

		if (config["debug_log"].as<bool>()) {
			logger.set_level(seastar::log_level::debug);
		} else {
			logger.set_level(seastar::log_level::info);
		}

		// print options
		std::cout << "======= TLOG server options ======\n";
		std::cout << "tcp port = " << port <<"\n";
		std::cout << "objstor_addr = " << objstor_addr << "\n";
		std::cout << "objstor_port = " << objstor_port << "\n";
		std::cout << "erasure encoding K="<< tlog::K << ". M = " << tlog::M << "\n";
		std::cout << "flush size = " << tlog::FLUSH_TIME << " packets\n";
		std::cout << "flush time = " << tlog::FLUSH_TIME << " seconds\n";
		std::cout << "private key = " << priv_key << "\n";
		std::cout << "debug log enabled = " << config["debug_log"].as<bool>() << "\n";
		std::cout << "==================================\n";

        return system_stats.start(tlog::clock_type::now()).then([&] {
            std::cout << PLATFORM << " tlog " << VERSION << "\n";
            return make_ready_future<>();
        }).then([&, port, objstor_port, objstor_addr, priv_key] {
            return tcp_server.start(std::ref(system_stats), objstor_addr, objstor_port,
					priv_key, port);
        }).then([&tcp_server] {
            return tcp_server.invoke_on_all(&tlog::tcp_server::start);
        }).then([start_stats = config.count("stats")] {
            return repeat([] {
                return periodic_flush().then([] { return stop_iteration::no; });
            });
        });
    });
}
