CC=g++-5
CPPFLAGS=-std=c++14 $(shell pkg-config --cflags $(SEASTAR)/build/release/seastar.pc)
LDLIBS=-std=c++14 $(shell pkg-config --libs $(SEASTAR)/build/release/seastar.pc)
LDLIBS += -lpthread -lcapnp -lkj -lcapnp-rpc -lb2 -lisal -lsnappy -lisal_crypto 

main: main.o tlog_schema.capnp.o redis_conn.o flusher.o packet_cache.o
	$(CC) -o main main.o tlog_schema.capnp.o redis_conn.o flusher.o packet_cache.o $(LDLIBS)

tlog_schema.capnp.o: tlog_schema.capnp.c++ tlog_schema.capnp.h
	$(CC) $(CPPFLAGS) -c tlog_schema.capnp.c++

redis_conn.o: redis_conn.cc redis_conn.h
	$(CC) $(CPPFLAGS) -c redis_conn.cc

flusher.o: flusher.cc flusher.h
	$(CC) $(CPPFLAGS) -c flusher.cc

packet_cache.o: packet_cache.cc packet_cache.h
	$(CC) $(CPPFLAGS) -c packet_cache.cc

main.o: main.cc
	$(CC) $(CPPFLAGS) -c main.cc

clean:
	rm -vf *.o main
