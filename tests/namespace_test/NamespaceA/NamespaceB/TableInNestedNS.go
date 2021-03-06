// Code generated by the FlatBuffers compiler. DO NOT EDIT.

package NamespaceB

import (
	flatbuffers "github.com/google/flatbuffers/go"
)

type TableInNestedNST struct {
	Foo int32
}

// TableInNestedNST object pack function
func (t *TableInNestedNST) Pack(builder *flatbuffers.Builder) flatbuffers.UOffsetT {
	if t == nil {
		return 0
	}

	// pack process all field

	TableInNestedNSStart(builder)
	TableInNestedNSAddFoo(builder, t.Foo)
	return TableInNestedNSEnd(builder)
}

// TableInNestedNST object unpack function
func (rcv *TableInNestedNS) UnPackTo(t *TableInNestedNST) {
	t.Foo = rcv.Foo()
}

func (rcv *TableInNestedNS) UnPack() *TableInNestedNST {
	if rcv == nil {
		return nil
	}
	t := &TableInNestedNST{}
	rcv.UnPackTo(t)
	return t
}

type TableInNestedNS struct {
	_tab flatbuffers.Table
}

// GetRootAsTableInNestedNS shortcut to access root table
func GetRootAsTableInNestedNS(buf []byte, offset flatbuffers.UOffsetT) *TableInNestedNS {
	n := flatbuffers.GetUOffsetT(buf[offset:])
	x := &TableInNestedNS{}
	x.Init(buf, n+offset)
	return x
}

// GetTableVectorAsTableInNestedNS shortcut to access table in vector of  unions
func GetTableVectorAsTableInNestedNS(table *flatbuffers.Table) *TableInNestedNS {
	n := flatbuffers.GetUOffsetT(table.Bytes[table.Pos:])
	x := &TableInNestedNS{}
	x.Init(table.Bytes, n+table.Pos)
	return x
}

// GetTableAsTableInNestedNS shortcut to access table in single union field
func GetTableAsTableInNestedNS(table *flatbuffers.Table) *TableInNestedNS {
	x := &TableInNestedNS{}
	x.Init(table.Bytes, table.Pos)
	return x
}

func (rcv *TableInNestedNS) Init(buf []byte, i flatbuffers.UOffsetT) {
	rcv._tab.Bytes = buf
	rcv._tab.Pos = i
}

func (rcv *TableInNestedNS) Table() flatbuffers.Table {
	return rcv._tab
}

func (rcv *TableInNestedNS) Foo() int32 {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(4))
	if o != 0 {
		return rcv._tab.GetInt32(o + rcv._tab.Pos)
	}
	return 0
}

func (rcv *TableInNestedNS) MutateFoo(n int32) bool {
	return rcv._tab.MutateInt32Slot(4, n)
}

func TableInNestedNSStart(builder *flatbuffers.Builder) {
	builder.StartObject(1)
}

func TableInNestedNSAddFoo(builder *flatbuffers.Builder, foo int32) {
	builder.PrependInt32Slot(0, foo, 0)
}

func TableInNestedNSEnd(builder *flatbuffers.Builder) flatbuffers.UOffsetT {
	return builder.EndObject()
}
