#include "redis_conn.h"
#include "core/sleep.hh"

#include <sstream>

extern seastar::logger logger;

static const auto endline = std::string("\r\n");

static const auto set_prefix = std::string("*3") + endline + std::string("$3") + endline +
					std::string("set") + endline;

/** number of seconds to sleep before we try to reconnect to redis */
static const int REDIS_CONN_SLEEP_SEC = 4;

static const int RECONECT_NUM = 2;

/**
 * format a redis SET command
 */
std::string format_set(const char *key, int key_len, const char *val, int val_len) {
	std::stringstream ss;
	ss << set_prefix;

	// key
	ss << "$" << key_len  << endline;
	ss << std::string(key, key_len) + endline;

	// val
	ss << "$" << val_len << endline;
	ss << std::string(val, val_len) << endline;

	return ss.str();
}

/**
 * format a redis GET command
 */
std::string format_get(const std::string& key) {
	std::stringstream ss;
	// prefix
	ss << "*2" << endline;

	// set command
	ss << "$3" << endline << "get" << endline;

	// key
	ss << "$" << key.length()  << endline;
	ss << key + endline;

	return ss.str();
}

redis_conn::redis_conn(socket_address sa, int idx)
	: _sa(sa)
	, _idx(idx)
{
	reconnect(RECONECT_NUM);
}

future<> redis_conn::reconnect(int retry_quota) {
		return connect(_sa).then([this, retry_quota] (connected_socket s) {
			_fd = std::move(s);
			_fd.set_keepalive(true);
			_out = _fd.output();
			_in = _fd.input();
			return make_ready_future<>();
		}).then_wrapped([this, retry_quota] (auto &&f) -> future<> {
			try {
				f.get();
			} catch(...) {
				logger.error("Failed to connect to redis `{}` retry quota:{}", _idx, retry_quota);
				if (retry_quota > 0) {
					return sleep(std::chrono::seconds(REDIS_CONN_SLEEP_SEC)).then([this, retry_quota] {
							return this->reconnect(retry_quota - 1);
						});
				} 
				else {
					logger.error("tlog exiting because failed to connect to redis :{}", _idx);
					exit(1);
				}
			}
			return make_ready_future<>();
		});
}

future<bool> redis_conn::set(const std::string& key, const std::string& val) {
	return set(key.c_str(), key.length(), val.c_str(), val.length());
}

future<bool> redis_conn::set(const char *key, int key_len, 
		const char *val, int val_len) {

	auto data = format_set(key, key_len, val, val_len);

	return this->send_read(data).then([this] (auto buf) {
			auto str = std::string(buf.get(), buf.size());
			return make_ready_future<bool>(str=="+OK\r\n");
		});
}



future<bool> redis_conn::get(const std::string& key, uint8_t *val, unsigned int val_len) {
	auto data = format_get(key);

	return this->send_read(data).then([this, val, val_len] (auto buf) {
			auto ok = false;
			auto prefix_len = 1 /* $ */ + floor(log10(val_len)) + 1 /* val_len */ + endline.length();
			if (buf && buf.size() == val_len + prefix_len) {
				std::memcpy(val, buf.get(), buf.size());
				ok = true;
			}
			return make_ready_future<bool>(ok);
		});
}

/**
 * send command to redis and read the response.
 * it handles all exception or empty response by reconnecting to redis server.
 */
future<temporary_buffer<char>> redis_conn::send_read(const std::string& command) {
	auto tbuf = temporary_buffer<char>(command.c_str(), command.length());
	return _out.write(std::move(tbuf)).then([this, command] {
			return _out.flush();
		}).then([this, command] {
			return _in.read();
		}).then([this, command] (auto buf) {
			return make_ready_future<temporary_buffer<char>>(std::move(buf));
		}).then_wrapped([this, command] (auto &&f) -> future<temporary_buffer<char>>{
			std::tuple<temporary_buffer<char>> result;
			auto catched = false;
			try {
				result =  f.get();
			} catch (...) {
				catched = true;
			}
			auto buf_result = std::move(std::get<0>(result));
			
			if (catched == true || buf_result.size() == 0) {
				logger.error("redis send_read failed .idx={}", this->_idx);
				return this->reconnect(RECONECT_NUM).then([this, command] {
						return this->send_read(command);
					});
			}

			return make_ready_future<temporary_buffer<char>>(std::move(buf_result));
		});

}

