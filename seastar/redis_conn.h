#ifndef _REDIS_CONN_H
#define _REDIS_CONN_H
#include "core/seastar.hh"
#include "core/reactor.hh"
#include "core/future-util.hh"
#include "core/app-template.hh"

class redis_conn {
private:
	connected_socket _fd;
	output_stream<char> _out;
	input_stream<char> _in;
	socket_address _sa;
	int _idx;
	bool _connected;
public:
	redis_conn(socket_address sa, int idx);
	future<bool> set(const std::string& key, const std::string& val);
	future<bool> set(const char *key, int key_len, const char *val, int val_len);
	future<bool> get(const std::string& key, uint8_t *val, unsigned int val_len);
	future<temporary_buffer<char>> send_read(const std::string& command);
private:
	future<> reconnect(int retry_quota=0);
};

#endif
