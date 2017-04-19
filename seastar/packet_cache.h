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
	uint32_t _vol_id_number;
	
	packet_cache(std::string& vol_id, uint32_t vol_id_num)
	:_vol_id(vol_id)
	,_vol_id_number(vol_id_num)
	{
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
