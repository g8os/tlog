# GIG TLOG  [![Build Status](https://travis-ci.org/g8os/tlog.svg?branch=master)](https://travis-ci.org/g8os/tlog)
Transaction log server

## TLOG Client

- TLOG Client library is a client lib that works on GIG BLOCKSTOR (NBD Server)
- TLOG Client communicates with TLOG Server via dpdk stack.
- Communication between client and server use binary capnp stream (no RPC).

### Data send from the client to server:
```
Block:
  - volume ID: uint32
  - sequence : uint64
  - LBA :uint64
  - Size :uint64
  - crc32 :uint32
  - data :Data (16K)
  - timestamp :uint64
 ```

## TLOG Server

- TLOG Server should store received log entries and aggregate them in a capnp structure in memmory
- After storing log entry it replies to the client on successfull transaction.
- after timeout or size of the aggregation is reached, we flush to OBJSTOR on top of ardb with forstdb datastore.
- Ideal setup would be to spread erasure coded blocks on different disks.
- For erasurecoding we split data blocks for 20 parts
- We use 20 instance OBJSTOR cluster to keep data blocks
- Each instance is used to keep erasure coded part according to its index (erasure coded part index == OBJSTOR instance index)
- We keep only backward links in our blockchain of history. We will add separate forward lining structure later in case it will be needed for the speed of recovery

### Settings

flush time: how maximum time we can wait data before force flush  
max size: maximum database size before force flush

### Tlog Aggregation structure:
Tlog aggregation per volume
```
Name (Text)        // unused now
Size (uint64)      // number of blocks
Timestamp (uint64)
Volume ID (uint32)
Blocks: List(Block)
```

## Workflow during flush

1. Hashing content (1) (blockhain style)
 - If this is the first block, Hash (1) will be defined-private-key, 
   othwise it's the has of the previous log entry.
2. Compression
3. Encryption (Key (1))
4. Hashing encrypted block (2)
5. Erasure coding the full payload
6. Saving each parts to ForestDB ARDB Backend
