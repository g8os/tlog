package client

// AUTO GENERATED - DO NOT EDIT

import (
	capnp "zombiezen.com/go/capnproto2"
	text "zombiezen.com/go/capnproto2/encoding/text"
	schemas "zombiezen.com/go/capnproto2/schemas"
)

type TlogResponse struct{ capnp.Struct }

// TlogResponse_TypeID is the unique identifier for the type TlogResponse.
const TlogResponse_TypeID = 0x98d11ae1c78a24d9

func NewTlogResponse(s *capnp.Segment) (TlogResponse, error) {
	st, err := capnp.NewStruct(s, capnp.ObjectSize{DataSize: 8, PointerCount: 1})
	return TlogResponse{st}, err
}

func NewRootTlogResponse(s *capnp.Segment) (TlogResponse, error) {
	st, err := capnp.NewRootStruct(s, capnp.ObjectSize{DataSize: 8, PointerCount: 1})
	return TlogResponse{st}, err
}

func ReadRootTlogResponse(msg *capnp.Message) (TlogResponse, error) {
	root, err := msg.RootPtr()
	return TlogResponse{root.Struct()}, err
}

func (s TlogResponse) String() string {
	str, _ := text.Marshal(0x98d11ae1c78a24d9, s.Struct)
	return str
}

func (s TlogResponse) Type() uint8 {
	return s.Struct.Uint8(0)
}

func (s TlogResponse) SetType(v uint8) {
	s.Struct.SetUint8(0, v)
}

func (s TlogResponse) Sequences() (capnp.UInt64List, error) {
	p, err := s.Struct.Ptr(0)
	return capnp.UInt64List{List: p.List()}, err
}

func (s TlogResponse) HasSequences() bool {
	p, err := s.Struct.Ptr(0)
	return p.IsValid() || err != nil
}

func (s TlogResponse) SetSequences(v capnp.UInt64List) error {
	return s.Struct.SetPtr(0, v.List.ToPtr())
}

// NewSequences sets the sequences field to a newly
// allocated capnp.UInt64List, preferring placement in s's segment.
func (s TlogResponse) NewSequences(n int32) (capnp.UInt64List, error) {
	l, err := capnp.NewUInt64List(s.Struct.Segment(), n)
	if err != nil {
		return capnp.UInt64List{}, err
	}
	err = s.Struct.SetPtr(0, l.List.ToPtr())
	return l, err
}

// TlogResponse_List is a list of TlogResponse.
type TlogResponse_List struct{ capnp.List }

// NewTlogResponse creates a new list of TlogResponse.
func NewTlogResponse_List(s *capnp.Segment, sz int32) (TlogResponse_List, error) {
	l, err := capnp.NewCompositeList(s, capnp.ObjectSize{DataSize: 8, PointerCount: 1}, sz)
	return TlogResponse_List{l}, err
}

func (s TlogResponse_List) At(i int) TlogResponse { return TlogResponse{s.List.Struct(i)} }

func (s TlogResponse_List) Set(i int, v TlogResponse) error { return s.List.SetStruct(i, v.Struct) }

// TlogResponse_Promise is a wrapper for a TlogResponse promised by a client call.
type TlogResponse_Promise struct{ *capnp.Pipeline }

func (p TlogResponse_Promise) Struct() (TlogResponse, error) {
	s, err := p.Pipeline.Struct()
	return TlogResponse{s}, err
}

type TlogBlock struct{ capnp.Struct }

// TlogBlock_TypeID is the unique identifier for the type TlogBlock.
const TlogBlock_TypeID = 0x8cf178de3c82d431

func NewTlogBlock(s *capnp.Segment) (TlogBlock, error) {
	st, err := capnp.NewStruct(s, capnp.ObjectSize{DataSize: 40, PointerCount: 1})
	return TlogBlock{st}, err
}

func NewRootTlogBlock(s *capnp.Segment) (TlogBlock, error) {
	st, err := capnp.NewRootStruct(s, capnp.ObjectSize{DataSize: 40, PointerCount: 1})
	return TlogBlock{st}, err
}

func ReadRootTlogBlock(msg *capnp.Message) (TlogBlock, error) {
	root, err := msg.RootPtr()
	return TlogBlock{root.Struct()}, err
}

func (s TlogBlock) String() string {
	str, _ := text.Marshal(0x8cf178de3c82d431, s.Struct)
	return str
}

func (s TlogBlock) VolumeId() uint32 {
	return s.Struct.Uint32(0)
}

func (s TlogBlock) SetVolumeId(v uint32) {
	s.Struct.SetUint32(0, v)
}

func (s TlogBlock) Sequence() uint64 {
	return s.Struct.Uint64(8)
}

func (s TlogBlock) SetSequence(v uint64) {
	s.Struct.SetUint64(8, v)
}

func (s TlogBlock) Lba() uint64 {
	return s.Struct.Uint64(16)
}

func (s TlogBlock) SetLba(v uint64) {
	s.Struct.SetUint64(16, v)
}

func (s TlogBlock) Size() uint32 {
	return s.Struct.Uint32(4)
}

func (s TlogBlock) SetSize(v uint32) {
	s.Struct.SetUint32(4, v)
}

func (s TlogBlock) Crc32() uint32 {
	return s.Struct.Uint32(24)
}

func (s TlogBlock) SetCrc32(v uint32) {
	s.Struct.SetUint32(24, v)
}

func (s TlogBlock) Data() ([]byte, error) {
	p, err := s.Struct.Ptr(0)
	return []byte(p.Data()), err
}

func (s TlogBlock) HasData() bool {
	p, err := s.Struct.Ptr(0)
	return p.IsValid() || err != nil
}

func (s TlogBlock) SetData(v []byte) error {
	return s.Struct.SetData(0, v)
}

func (s TlogBlock) Timestamp() uint64 {
	return s.Struct.Uint64(32)
}

func (s TlogBlock) SetTimestamp(v uint64) {
	s.Struct.SetUint64(32, v)
}

// TlogBlock_List is a list of TlogBlock.
type TlogBlock_List struct{ capnp.List }

// NewTlogBlock creates a new list of TlogBlock.
func NewTlogBlock_List(s *capnp.Segment, sz int32) (TlogBlock_List, error) {
	l, err := capnp.NewCompositeList(s, capnp.ObjectSize{DataSize: 40, PointerCount: 1}, sz)
	return TlogBlock_List{l}, err
}

func (s TlogBlock_List) At(i int) TlogBlock { return TlogBlock{s.List.Struct(i)} }

func (s TlogBlock_List) Set(i int, v TlogBlock) error { return s.List.SetStruct(i, v.Struct) }

// TlogBlock_Promise is a wrapper for a TlogBlock promised by a client call.
type TlogBlock_Promise struct{ *capnp.Pipeline }

func (p TlogBlock_Promise) Struct() (TlogBlock, error) {
	s, err := p.Pipeline.Struct()
	return TlogBlock{s}, err
}

type TlogAggregation struct{ capnp.Struct }

// TlogAggregation_TypeID is the unique identifier for the type TlogAggregation.
const TlogAggregation_TypeID = 0xe46ab5b4b619e094

func NewTlogAggregation(s *capnp.Segment) (TlogAggregation, error) {
	st, err := capnp.NewStruct(s, capnp.ObjectSize{DataSize: 24, PointerCount: 3})
	return TlogAggregation{st}, err
}

func NewRootTlogAggregation(s *capnp.Segment) (TlogAggregation, error) {
	st, err := capnp.NewRootStruct(s, capnp.ObjectSize{DataSize: 24, PointerCount: 3})
	return TlogAggregation{st}, err
}

func ReadRootTlogAggregation(msg *capnp.Message) (TlogAggregation, error) {
	root, err := msg.RootPtr()
	return TlogAggregation{root.Struct()}, err
}

func (s TlogAggregation) String() string {
	str, _ := text.Marshal(0xe46ab5b4b619e094, s.Struct)
	return str
}

func (s TlogAggregation) Name() (string, error) {
	p, err := s.Struct.Ptr(0)
	return p.Text(), err
}

func (s TlogAggregation) HasName() bool {
	p, err := s.Struct.Ptr(0)
	return p.IsValid() || err != nil
}

func (s TlogAggregation) NameBytes() ([]byte, error) {
	p, err := s.Struct.Ptr(0)
	return p.TextBytes(), err
}

func (s TlogAggregation) SetName(v string) error {
	return s.Struct.SetText(0, v)
}

func (s TlogAggregation) Size() uint64 {
	return s.Struct.Uint64(0)
}

func (s TlogAggregation) SetSize(v uint64) {
	s.Struct.SetUint64(0, v)
}

func (s TlogAggregation) Timestamp() uint64 {
	return s.Struct.Uint64(8)
}

func (s TlogAggregation) SetTimestamp(v uint64) {
	s.Struct.SetUint64(8, v)
}

func (s TlogAggregation) VolumeId() uint32 {
	return s.Struct.Uint32(16)
}

func (s TlogAggregation) SetVolumeId(v uint32) {
	s.Struct.SetUint32(16, v)
}

func (s TlogAggregation) Blocks() (TlogBlock_List, error) {
	p, err := s.Struct.Ptr(1)
	return TlogBlock_List{List: p.List()}, err
}

func (s TlogAggregation) HasBlocks() bool {
	p, err := s.Struct.Ptr(1)
	return p.IsValid() || err != nil
}

func (s TlogAggregation) SetBlocks(v TlogBlock_List) error {
	return s.Struct.SetPtr(1, v.List.ToPtr())
}

// NewBlocks sets the blocks field to a newly
// allocated TlogBlock_List, preferring placement in s's segment.
func (s TlogAggregation) NewBlocks(n int32) (TlogBlock_List, error) {
	l, err := NewTlogBlock_List(s.Struct.Segment(), n)
	if err != nil {
		return TlogBlock_List{}, err
	}
	err = s.Struct.SetPtr(1, l.List.ToPtr())
	return l, err
}

func (s TlogAggregation) Prev() ([]byte, error) {
	p, err := s.Struct.Ptr(2)
	return []byte(p.Data()), err
}

func (s TlogAggregation) HasPrev() bool {
	p, err := s.Struct.Ptr(2)
	return p.IsValid() || err != nil
}

func (s TlogAggregation) SetPrev(v []byte) error {
	return s.Struct.SetData(2, v)
}

// TlogAggregation_List is a list of TlogAggregation.
type TlogAggregation_List struct{ capnp.List }

// NewTlogAggregation creates a new list of TlogAggregation.
func NewTlogAggregation_List(s *capnp.Segment, sz int32) (TlogAggregation_List, error) {
	l, err := capnp.NewCompositeList(s, capnp.ObjectSize{DataSize: 24, PointerCount: 3}, sz)
	return TlogAggregation_List{l}, err
}

func (s TlogAggregation_List) At(i int) TlogAggregation { return TlogAggregation{s.List.Struct(i)} }

func (s TlogAggregation_List) Set(i int, v TlogAggregation) error {
	return s.List.SetStruct(i, v.Struct)
}

// TlogAggregation_Promise is a wrapper for a TlogAggregation promised by a client call.
type TlogAggregation_Promise struct{ *capnp.Pipeline }

func (p TlogAggregation_Promise) Struct() (TlogAggregation, error) {
	s, err := p.Pipeline.Struct()
	return TlogAggregation{s}, err
}

const schema_f4533cbae6e08506 = "x\xdat\x93OH\x15_\x1c\xc5\xcf\xf9\xde7\xa3\x82" +
	"\xfa~\xe3{\x8b_a\x04a\x0b%\xc3?;1\xcc" +
	"V\xd5\xca\xabA\xcb\x18\xc7\xcb\xcb|\xef\xcd\xe4\x8c\xf6" +
	"g\x19\xb4\x89\xa0\x8d\x0b[\x04\x06-\x0a\xdc\x04\x1a\x14" +
	"\x05\x06I\x06\x06\x09\x06\xb5\x09\x8bh\x1f\xb4j3q" +
	"}\xcd\xf3\x15\xb6\xfb\xce\xe1\xcc=\xdf{>\xdc\xbe\x09" +
	"\x9e\x94~'\x14@w:n\xda\xbf}c\xf8\xd3\xd5" +
	"\xef\xb7\xa1;\xe8\xa4\xee\xcd\x9do\xcf\x86'~\xc0a" +
	"\x13P8\xc4/\x85n;\x0d\x1e\xe5\x1d\x82\xe9\xc7\xae" +
	"[\xaf?\x1f\xdcZ\xb4n\xfe\xed~*?\x0b\xebb" +
	"\xa7\x97r\x05L\x17v\x0e<YY\xbd\xf4\xd5\x9aU" +
	"\x83Y\xd9\x03\xfbU\x07\x0b\xa3\xbb\xe3\x09u\x9e\xe8M" +
	"\x93rX\xba\x10\x07\x17\xc5T\xfc\xe3\x81\x1fU\xa3\xa1" +
	"s\xe5\xb0t\xaa\xdc\x14\x063c\xa4\xeeT9 G" +
	"\xc0[=\x0b\xe8\x15E\xbd&\xf4\xc8\"\xad\xf8\xc2\x8a" +
	"\xcf\x15\xf5\x86\xd0\x13)R\x00o\xfd\x08\xa0\xd7\x14\xf5" +
	"\xa6\xd0S,R\x01\xde\x9b\x1e@\xbfR\xd4\xef\x84^" +
	"\xce-2\x07xo\x07\x00\xbd\xa1\xa8\xb7\x85t\x8at" +
	"\x00o\xcb\x1a7\x15\xf5\x07\xa1\xe7\xe6\x8at\x01\xef\xfd" +
	"8\xa0\xb7\x15\xf5\x8e0\x9d\x0f\xcbs\x15sf\x0a\x00" +
	"\x9b!l\x06\xd3\xd8\\\x9e3\xd5\xc0X\xad\x05\xc2\x16" +
	"\xb0\xa9<\xe9gs>\x9e\xben2\xf3\xe1`6\x18" +
	"\x1c\xc8\xbe\xf2S~\xe2\xb3\x0d\xc260M\xa6+&" +
	"N\xfc\x0a\x18e\xff\xfe\xab\xa4q3\x12Ga56" +
	"\xb6\xa7\xe6zO\xddv\xff.E\xdd'\xccj\xea\xb5" +
	"\xeb\x1fS\xd4\xa7\x85\xf9\xe4Zd\xe8B\xe86\xee\xcd" +
	"\x98\xed\xe0\x98\xe2nl{C\xac\xfa3v\xb4T\x9a" +
	"5%?\x99\x0eY\xb5\xc9\xff\xd7\x93\xef\xda\xe4\x05E" +
	"\xbd\xb4\x97|\xcfj\x8b\x8a\xfa\x81\x05\xc4\x1a\xa0\xfbv" +
	"\x9d%E\xbdl\x01\xe5j\x80\x1eY\x94\x0f\x15\xf5\x8a" +
	"\x05\xc4\x1a\xa0\xc7C\x80^\xfe\xcd\xd7\x91\x1a\xa1\xf5\x9e" +
	"=\xbe\xf9\xaa_1l\x85\xb05k\xb9^\xdb>U" +
	"\xee\x83nd\xb2\x1c\x063\xf5\xdb\xff\xb7\xf7:@+" +
	"\xe6\xa3Y3\x9f\xf1\xf9\x15\x00\x00\xff\xff\xe35\xb7l"

func init() {
	schemas.Register(schema_f4533cbae6e08506,
		0x8cf178de3c82d431,
		0x98d11ae1c78a24d9,
		0xe46ab5b4b619e094)
}
