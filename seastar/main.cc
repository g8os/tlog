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


#include "redis_conn.h"
#include "flusher.h"

#define PLATFORM "seastar"
#define VERSION "v1.0"
#define VERSION_STRING PLATFORM " " VERSION

future<> slow() {
    return sleep(std::chrono::seconds(10000000));
}
namespace tlog {

static std::vector<void *> servers;

using clock_type = lowres_clock;

const int BUF_SIZE = 16472; /* size of the message we receive from client */

/* erasure encoding variable */
int K = 4;
int M = 2;

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
    tcp_server(distributed<system_stats>& system_stats, std::string objstor_addr,
			int objstor_port, std::string priv_key, uint16_t port = 11211)
        : _system_stats(system_stats)
		, _objstor_addr(objstor_addr)
		, _objstor_port(objstor_port)
		, _priv_key(priv_key)
        , _port(port)
    {
		_flusher = Flusher(_objstor_addr, _objstor_port, _priv_key, K, M);
		
		// TODO : for some unknown reason, it can't be put into constructor
		_flusher.post_init();
		std::cout << "TCP server started at core " << engine().cpu_id() << "\n";
	}

    void start() {
        listen_options lo;
        lo.reuse_address = true;
        _listener = engine().listen(make_ipv4_address({_port}), lo);
        keep_doing([this] {
            return _listener->accept().then([this] (connected_socket fd, socket_address addr) mutable {
                auto conn = make_lw_shared<connection>(std::move(fd), addr, _system_stats);
                do_until([conn] { return conn->_in.eof(); }, [this, conn] {
                    return this->handle(conn->_in, conn->_out).then([conn] {
                        return conn->_out.flush();
                    });
                }).finally([conn] {
                    return conn->_out.close().finally([conn]{});
                });
            });
        }).or_terminate();
    }

    future<> stop() { return make_ready_future<>(); }

	/**
	 * handle incoming packet
	 */
	future<> handle(input_stream<char>& in, output_stream<char>& out) {
    	return repeat([this, &out, &in] {
			// this malloc will be freed in 'flush'
			uint8_t *packet = (uint8_t *) malloc(BUF_SIZE);
        	return in.read_exactly(BUF_SIZE).then( [this, &out, packet] (temporary_buffer<char> buf) {
				// Check if we receive data with expected size.
				// Unexpected size indicated broken client/connection,
				// we close it for simplicity.
            	if (buf && buf.size() == BUF_SIZE) {
					std::memcpy(packet, buf.get(), buf.size());
					return handle_packet(packet).then([]{
						return make_ready_future<stop_iteration>(stop_iteration::no);
					});
            	} else {
                	return make_ready_future<stop_iteration>(stop_iteration::yes);
            	}
        	});
    	}).then([&out] {
        	return make_ready_future<>();
    	});

	}

	future<> handle_packet(uint8_t *packet) {
		// get volume ID
		uint32_t vol_id;
		uint64_t seq;
		memcpy(&vol_id, packet + 24, 4);
		memcpy(&seq, packet + 32, 8);

		return smp::submit_to(vol_id % smp::count, [packet, vol_id, seq] {
				auto flusher = get_flusher(engine().cpu_id());
				flusher->add_packet(packet, vol_id, seq);
				return flusher->check_do_flush(vol_id);
				});
	}

private:
};


} /* namespace tlog */

int main(int ac, char** av) {
    distributed<tlog::system_stats> system_stats;
    distributed<tlog::tcp_server> tcp_server;

	namespace bpo = boost::program_options;
    app_template app;

	app.add_options()
		("port", bpo::value<uint16_t>()->default_value(11211), "tcp port")
		("k", bpo::value<int>()->default_value(4), "K variable of erasure encoding")
		("m", bpo::value<int>()->default_value(2), "M variable of erasure encoding")
		("objstor_addr", bpo::value<std::string>()->default_value("127.0.0.1"), "objstor address")
		("objstor_port", bpo::value<int>()->default_value(16379), "objstor first port")
		("priv_key", bpo::value<std::string>()->default_value("my-secret-key"), "private key")

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

		// print options
		std::cout << "======= TLOG server options ======\n";
		std::cout << "tcp port = " << port <<"\n";
		std::cout << "objstor_addr = " << objstor_addr << "\n";
		std::cout << "objstor_port = " << objstor_port << "\n";
		std::cout << "erasure encoding K="<< tlog::K << ". M = " << tlog::M << "\n";
		std::cout << "private key = " << priv_key << "\n";
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
            // what we really wanted to do here is to
            // avoid the server to exit
            // need a better way here than sleeping
            return repeat([] {
                return slow().then([] { return stop_iteration::no; });
            });
        });
    });
}
