#ifndef _connection_h_
#define _connection_h_

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

	void set_vol_id(std::string& vol_id) {
		if (_vol_id_num > 0) {
			return;
		}
		_vol_id = vol_id;
		
		std::hash<std::string> fn;
		_vol_id_num = (uint32_t) fn(_vol_id);
	}
};
#endif
