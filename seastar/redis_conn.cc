#include "redis_conn.h"

/**
 * this is very naive & inefficient string formatting.
 * TODO : optimize it!
 */
std::string format_set(const uint8_t *key, int key_len, const uint8_t *val, int val_len) {
	// prefix
	auto str = std::string("*3\r\n");

	// set command
	str += std::string("$3\r\nset\r\n");

	// key
	str += std::string("$") + std::to_string(key_len) + std::string("\r\n");
	str += std::string((const char *)key, key_len) + std::string("\r\n");

	// val
	str += std::string("$") + std::to_string(val_len) + std::string("\r\n");
	str += std::string((const char *)val, val_len) + std::string("\r\n");

	return str;
}

redis_conn::redis_conn(connected_socket&& fd)
: _fd(std::move(fd))
, _out(_fd.output())
, _in(_fd.input())
{
	_fd.set_keepalive(true);
}

future<bool> redis_conn::set(std::string key, std::string val) {
	return set((const uint8_t *) key.c_str(), key.length(), 
			(const uint8_t *) val.c_str(), val.length());
}

future<bool> redis_conn::set(const uint8_t *key, int key_len, 
		const uint8_t *val, int val_len) {

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
