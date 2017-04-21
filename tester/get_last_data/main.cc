#include <iostream>
#include <string>
#include <string.h>
#include <vector>
#include <hiredis/hiredis.h>
#include <stdlib.h>
#include <assert.h>
#include <snappy.h>
#include <cstring>
#include <isa-l_crypto/aes_cbc.h>
#include <isa-l_crypto/aes_keyexp.h>

#include "tlog_schema.capnp.h"
#include <capnp/message.h>
#include <kj/io.h>
#include <kj/common.h>
#include <capnp/serialize-packed.h>
#include <isa-l/erasure_code.h>
const std::string REDIS_ADDR = "127.0.0.1";
const int REDIS_PORT = 16379;
const int k = 4;
const int m = 2;

class ErasureDecoder {
private:
	unsigned char *decode_tabs;
	unsigned char *decode_matrix;
	int k;
	int m;
	std::vector<int> erasures;
public:
	ErasureDecoder(int kVal, int mVal, std::vector<int> erasuresVal) {
		k = kVal;
		m = mVal;
		erasures = erasuresVal;

		if ((int)erasures.size() > m) {
			std::cerr << "too much data lost\n";
			exit(1);
		}

		std::vector<bool> is_erased(k + m);
		for (int i=0; i < (int)erasures.size(); i++) {
			is_erased[erasures[i]] = true;
		}

		/* from encoder */
		unsigned char *_encode_matrix = (unsigned char *) malloc(k * (k + m));
		unsigned char *_encode_tab = (unsigned char *) malloc(32 * k  * ( k + m));
		gf_gen_cauchy1_matrix(_encode_matrix, k+m, k);
		ec_init_tables(k, m, &_encode_matrix[k * k], _encode_tab);
		/*******/
		
		unsigned char *smatrix = (unsigned char*)malloc(sizeof(unsigned char) * k * (k+m));
		unsigned char *invmatrix = (unsigned char*)malloc(sizeof(unsigned char) * k * (k+m));
		decode_matrix = (unsigned char *)malloc(sizeof(unsigned char) * k * (k+m));

		// remove damaged entries from the matrix
		int row = 0;
		for (int i = 0; i < (k+m); i++) {
			if (is_erased[i]) {
				continue;
			}
			for (int j=0; j < k; j++) {
				smatrix[k * row + j] = _encode_matrix[k * i + j];
			}
			row++;
		}

		int ok = gf_invert_matrix(smatrix, invmatrix, k);
		assert(ok == 0);

		// put damaged entries from the inverted matrix
		for (int i=0; i < (int)erasures.size();i++) {
			for (int j=0; j < k; j++) {
				decode_matrix[k * i + j] = invmatrix[k * i + j];
			}
		}

		decode_tabs = (unsigned char *) malloc( 32 * k * (k + m));
		ec_init_tables(k, erasures.size(), decode_matrix, decode_tabs);
	}

	unsigned char** decode(unsigned char **input, int input_len, int chunksize) {
		unsigned char **result = (unsigned char **) malloc(sizeof (unsigned char *) * erasures.size());
		for (unsigned int i=0; i < erasures.size(); i++) {
			result[i] = (unsigned char *) malloc(chunksize);
		}
		std::cout << "calling ec_encode\n";
		ec_encode_data(chunksize, k, erasures.size(), decode_tabs, input, result);
		return result;
	}
};
unsigned char *get_from_redis(std::string addr, int port, const unsigned char *key,
		int key_len, int *data_len) {
	redisContext *c;
	redisReply *reply;

	//std::cout << "connect to \n" ;
	c = redisConnect(addr.c_str(), port);
	reply = (redisReply *)redisCommand(c, "GET %b", key, key_len);
	
	if (reply->type == REDIS_REPLY_ERROR) {
		std::cout << "redis rep;y errpr\n";
		std::cout << "reply = " << reply->str << "\n";
	}

	unsigned char *result = (unsigned char *) malloc (reply->len);
	*data_len = reply-> len;
	memcpy(result, reply->str, reply->len);
	
	freeReplyObject(reply);
	redisFree(c);

	return result;
}

void decode_capnp(unsigned char *encoded, int encoded_len) {
	auto apt = kj::ArrayPtr<kj::byte>(encoded, encoded_len);
	kj::ArrayInputStream ais(apt);
	::capnp::MallocMessageBuilder message;
	readMessageCopy(ais, message);
	auto agg = message.getRoot<TlogAggregation>();
	std::cout << "agg size=" << agg.getSize() << ".name=" << agg.getName().cStr() << "\n";
	for (auto block : agg.getBlocks()) {
		std::cout<<"-> seq= " << block.getSequence() << " . vol id = " << block.getVolumeId().cStr() << ".";
		auto data = block.getData();
		std::cout<<"1st 3 bytes=" << data[0] << data[1] << data[2] << ".";
		std::cout << "\n";
	}
}

void test_encoded(unsigned char *hash, int hash_len) {
	// simulate we lost first data
	unsigned char **inputs = (unsigned char**) malloc(sizeof(unsigned char *) * (k+m-1));
	int data_len;
	// get k pieces
	for (int i = 1; i < k; i++) {
		inputs[i-1] = get_from_redis(REDIS_ADDR, REDIS_PORT + 1 +i,
				hash, hash_len, &data_len);
		std::cout << "data_len = " << data_len << "\n";
	}
	for (int i = 0; i < m; i++) {
		inputs[k+i-1] = get_from_redis(REDIS_ADDR, REDIS_PORT + 1 +i + k,
				hash, hash_len, &data_len);
		std::cout << "data_len = " << data_len << "\n";
	}
	std::vector<int> erasures;
	erasures.push_back(0);
	ErasureDecoder ed(k, m, erasures);
	unsigned char **recovered = ed.decode(inputs, k+m, data_len);

	// merge data back
	unsigned char *encoded = (unsigned char *) malloc(sizeof(char) * data_len * 4);
	memcpy(encoded, recovered[0], data_len);
	for (int i =1; i < k; i++) {
		memcpy(encoded + (data_len * i), inputs[i-1], data_len);
	}

	// decrypt
	uint8_t iv[16];
	std::memset(iv, '0', 16);
	uint8_t enc_key[256];
	uint8_t dec_key[256];
	auto priv_key = std::string("12345678901234567890123456789012");
	aes_keyexp_256((uint8_t *) priv_key.c_str(), enc_key, dec_key);
	
	unsigned char *unencrypted = (unsigned char *) malloc(sizeof (unsigned char) * data_len * 4);
	aes_cbc_dec_256(encoded, iv, dec_key, unencrypted, data_len * 4);

	// uncompress
	std::string uncompressed;
	snappy::Uncompress((const char *)unencrypted, data_len * 4, &uncompressed);

	// decode capnp
	decode_capnp((unsigned char *) uncompressed.c_str(), uncompressed.length());
}

int main() {
	// get hash from server
	int hash_len;
	std::string hashKey = "last_hash_12345678901234567890123456789012";
	unsigned char *hash = get_from_redis(REDIS_ADDR, REDIS_PORT, (const unsigned char *)hashKey.c_str(),
			hashKey.length(), &hash_len);
	test_encoded(hash, hash_len);
}

