#ifndef _tlog_connection_h_
#define _tlog_connection_h_

#include "core/app-template.hh"
#include "core/future-util.hh"
#include "core/timer-set.hh"
#include "core/shared_ptr.hh"
#include "core/units.hh"
#include "core/distributed.hh"
#include "core/bitops.hh"
#include "core/sleep.hh"




class connection {
public:
	connected_socket _socket;
	socket_address _addr;
	input_stream<char> _in;
	output_stream<char> _out;
	std::string _vol_id;
	uint32_t _vol_id_num;

	connection(connected_socket&& socket, socket_address addr)
		:_socket(std::move(socket))
		, _addr(addr)
		, _in(_socket.input())
		, _out(_socket.output())
		, _vol_id_num(0)
	{
	}

	~connection(){}

	/**
	 * Set volume id of this connection.
	 * We only set this if it is not set before.
	 */
	void set_vol_id(const char *vol_id, size_t vol_id_len) {
		if (_vol_id_num > 0) {
			return;
		}
		_vol_id = std::string(vol_id, vol_id_len);
		//_vol_id.resize(strlen(_vol_id.c_str()));
		
		std::hash<std::string> fn;
		_vol_id_num = (uint32_t) fn(_vol_id);
	}

	// read tlog block data from the client.
	future<temporary_buffer<char>> read() {
		// read the prefix
		// as described in https://capnproto.org/encoding.html#serialization-over-a-stream
		return _in.read_exactly(8).then([this] (auto buf) {
				// ignore first 4 bytes now because we only use single capnp segment
				uint32_t length;
				memcpy(&length, buf.begin() + 4, 4);

				// read the data
				return _in.read_exactly(length * 8);
		});
	}
};
#endif
