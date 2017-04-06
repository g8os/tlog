import reactor, capnp, collections/pprint, collections, reactor/redis, times, isa/erasure_code, isa/hash, securehash, reactor/threading, os, snappy, isa/aes
import tlog/schema, tlog/util

type
  CoreState = ref object
    dbConnections: seq[RedisClient]
    hasher: Sha512Pool
    volumes: Table[uint32, VolumeHandler]

  VolumeHandler = ref object
    volumeId: uint32
    lastHash: string
    waitingBlocks: seq[TlogBlock]
    flushing: bool

var coreState {.threadvar.}: CoreState

when defined(enableEncryption):
  let aesKey = expandAESKey("0".repeat(32))
const zeroIV = "0".repeat(32)

proc flush(volume: VolumeHandler): Future[void] {.async.} =
  if volume.flushing:
    return

  volume.flushing = true
  defer:
    volume.flushing = false

  if volume.waitingBlocks.len == 0:
    return

  if volume.lastHash == nil:
    # lastHash not yet fetched
    let lastHash = await coreState.dbConnections[0].hget("volume-hash", $(volume.volumeId))
    volume.lastHash = lastHash
    # if volume is still nil, this volume is empty

  let agg = TlogAggregation(blocks: volume.waitingBlocks,
                            volumeId: volume.volumeId,
                            prev: volume.lastHash)
  let aggData = packPointer(agg)
  #let aggHash = coreState.hasher.computeHashes(@[aggData])[0]
  let aggHash = $(secureHash(aggData))

  const
    totalChunks = 10
    maxLost = 3

  let aggDataCompressed = snappy.compress(aggData)
  var aggDataEncrypted = aggDataCompressed
  when defined(enableEncryption):
    aggDataEncrypted.setLen(aggDataEncrypted.len + 32 - (aggDataEncrypted.len mod 32))
    aggDataEncrypted = aesKey.encrypt(aggDataEncrypted)

  let chunks = erasure_code.encodeString(aggDataCompressed, totalChunks, maxLost)
  var waitFor: seq[Future[int64]] = @[]
  for i in 0..<totalChunks: # store in parallel
    waitFor.add coreState.dbConnections[i].hset("volume-data", aggHash, chunks[i])
  echo "waiting..."
  for fut in waitFor: discard (await fut)
  echo "saved (", waitFor.len, " blocks)"

  discard (await coreState.dbConnections[0].hset("volume-hash", $(volume.volumeId), aggHash))
  volume.lastHash = aggHash
  volume.waitingBlocks = @[]

proc handleMsgProc(blockMsg: TlogBlock): auto =
  return proc() =
           if blockMsg.volumeId notin coreState.volumes:
             echo "cache miss: ", blockMsg.volumeId
             coreState.volumes[blockMsg.volumeId] = VolumeHandler(waitingBlocks: @[], volumeId: blockMsg.volumeId)

           let vol = coreState.volumes[blockMsg.volumeId]
           vol.waitingBlocks.add(blockMsg)
           if vol.waitingBlocks.len > 100:
             vol.flush.ignore

proc handleClient(conn: TcpConnection) {.async.} =
  while true:
    let segmentsR = tryAwait readMultisegment(conn.input)
    if segmentsR.isError and segmentsR.error.getOriginal == JustClose: # eof?
      break

    let segments = await segmentsR
    let blockMsg = capnp.newUnpacker(segments).unpackPointer(0, TlogBlock)
    #echo "recv volumeId:", blockMsg.volumeId, " seq:", blockMsg.sequence
    # do we need to wait for this to finish before returning response?
    runOnThread((blockMsg.volumeId.int mod threadLoopCount()), handleMsgProc(blockMsg))

    let resp = TlogResponse(status: 0, sequences: @[blockMsg.sequence])
    let data = packPointer(resp)
    await conn.output.write($(data.len + 8) & "\r\n")
    await writeMultisegment(conn.output, data)

proc connectToRedis() {.async.} =
  for i in 1..10:
    let conn = await redis.connect("localhost", 11000 + i, reconnect=true)
    coreState.dbConnections.add(conn)

proc flushAll() {.async.} =
  for vol in coreState.volumes.values:
    await vol.flush

proc periodicFlusher() {.async.} =
  while true:
    await flushAll()
    await asyncSleep(1000)

proc loopMain() {.async.} =
  echo "init"
  coreState = CoreState(dbConnections: @[], hasher: newSha512Pool(),
                        volumes: initTable[uint32, VolumeHandler]())
  await connectToRedis()
  let server = await createTcpServer(11211, reusePort=true)

  periodicFlusher().ignore

  asyncFor conn in server.incomingConnections:
    echo "incoming connection ", conn.getPeerAddr
    handleClient(conn).ignore

proc main*() =
  startMultiloop(mainProc=loopMain, threadCount=4)

when isMainModule:
  main()
