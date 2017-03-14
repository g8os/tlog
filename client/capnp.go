package client

import (
	"bytes"
	"fmt"

	"zombiezen.com/go/capnproto2"
)

func decodeResponse(data []byte) (*TlogResponse, error) {
	msg, err := capnp.NewDecoder(bytes.NewBuffer(data)).Decode()
	if err != nil {
		return nil, err
	}

	tr, err := ReadRootTlogResponse(msg)
	return &tr, err
}

func buildCapnp(volID uint32, seq uint64, crc uint32,
	lba, timestamp uint64, data []byte) ([]byte, error) {
	msg, seg, err := capnp.NewMessage(capnp.MultiSegment(nil))
	if err != nil {
		return nil, fmt.Errorf("build capnp:%v", err)
	}

	block, err := NewRootTlogBlock(seg)
	if err != nil {
		return nil, fmt.Errorf("create block:%v", err)
	}

	block.SetVolumeId(volID)
	block.SetSequence(seq)
	block.SetLba(lba)
	block.SetCrc32(crc)
	block.SetTimestamp(timestamp)
	block.SetSize(uint32(len(data)))
	block.SetData(data)

	buf := new(bytes.Buffer)

	err = capnp.NewEncoder(buf).Encode(msg)
	return buf.Bytes(), err
}
