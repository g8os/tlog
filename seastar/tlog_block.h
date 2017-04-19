#ifndef __TLOG_BLOCK_H_
#define __TLOG_BLOCK_H_

#include <cstring>
#include "connection.h"
#include "tlog_schema.capnp.h"

struct tlog_block {
	std::string _vol_id;
	uint64_t _sequence;
	uint64_t _lba;
	uint32_t _size;
	uint32_t _crc;
	uint8_t *_data;
	uint64_t _timestamp;
public:
	tlog_block(connection *conn, TlogBlock::Builder *block) {
		_vol_id = conn->_vol_id;
		_sequence = block->getSequence();
		_lba = block->getLba();
		_size = block->getSize();
		_crc = block->getCrc32();
		_data = (uint8_t *) malloc (_size);
		std::memcpy(_data, block->getData().begin(), _size);
		_timestamp = block->getTimestamp();
	}

	~tlog_block() {
		free(_data);
	}
};
#endif
