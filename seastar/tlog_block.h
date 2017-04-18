#ifndef __TLOG_BLOCK_H_
#define __TLOG_BLOCK_H_

struct tlog_block {
	std::string _vol_id;
	uint64_t _sequence;
	uint64_t _lba;
	uint32_t _size;
	uint32_t _crc;
	uint8_t *_data;
	uint64_t _timestamp;
public:
	tlog_block(const char* vol_id, size_t vol_id_len, uint64_t seq, uint64_t lba, uint32_t size, uint32_t crc,
			uint8_t *data, uint64_t timestamp) {
		_vol_id = std::string(vol_id, vol_id_len);
		_sequence = seq;
		_lba = lba;
		_size = size;
		_crc = crc;
		_data = (uint8_t *) malloc (size);
		std::memcpy(_data, data, size);
		_timestamp = timestamp;
	}

	unsigned vol_id_number() {
		std::hash<std::string> fn;
		return (unsigned) fn(_vol_id);
	}
	~tlog_block() {
		free(_data);
	}
};
#endif
