#ifndef _packet_cache_h_
#define _packet_cache_h_
#include <functional>
#include <string>
#include <queue>
#include <map>

#include "tlog_block.h"

class packet_cache {
private:
	std::map<uint64_t, tlog_block *> _packets;
public:
	std::string _vol_id;
	size_t _vol_id_number;
	
	packet_cache(std::string& vol_id)
	:_vol_id(vol_id)
	{
		_vol_id.resize(strlen(_vol_id.c_str()));
		std::hash<std::string> hash_fn;
		_vol_id_number = hash_fn(_vol_id);
	}

	int size() {
		return _packets.size();
	}
	void add(tlog_block *tb) {
		_packets[tb->_sequence] = tb;
	}
	bool pick(std::queue<tlog_block *> *q, int flush_size);
};

#endif
