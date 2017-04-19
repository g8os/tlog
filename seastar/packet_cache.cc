#include "packet_cache.h"

bool packet_cache::pick(std::queue<tlog_block *> *q, int flush_size) {
	if (this->size() < flush_size) {
		return false;
	}
	
	auto it = _packets.begin();
	for (int i=0; i  < flush_size && it != _packets.end(); i++) {
		q->push(it->second);
		it = _packets.erase(it);
	}
	return true;
}
