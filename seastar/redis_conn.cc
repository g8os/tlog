#include "redis_conn.h"

#include <sstream>

const auto endline = std::string("\r\n");

const auto set_prefix = std::string("*3") + endline + std::string("$3") + endline +
					std::string("set") + endline;
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

redis_conn::redis_conn(connected_socket&& fd)
: _fd(std::move(fd))
, _out(_fd.output())
, _in(_fd.input())
{
	_fd.set_keepalive(true);
}

future<bool> redis_conn::set(const std::string& key, const std::string& val) {
	return set(key.c_str(), key.length(), val.c_str(), val.length());
}

future<bool> redis_conn::set(const char *key, int key_len, 
		const char *val, int val_len) {

	auto data = format_set(key, key_len, val, val_len);
	return _out.write(data).then([this] {
			return _out.flush();
		}).then([this] {
			return _in.read();
		}).then([this] (auto buf) {
			auto str = std::string(buf.get(), buf.size());
			return make_ready_future<bool>(str=="+OK\r\n");
		});
}
