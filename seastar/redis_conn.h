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
public:
	redis_conn(connected_socket&& fd);
	future<bool> set(const std::string& key, const std::string& val);
	future<bool> set(const char *key, int key_len, const char *val, int val_len);
};

#endif
