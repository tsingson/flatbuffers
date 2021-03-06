// Code generated by the FlatBuffers compiler. DO NOT EDIT.

package Movie

import (
	flatbuffers "github.com/google/flatbuffers/go"
)

type MovieT struct {
	MainCharacter *CharacterT
	Characters []*CharacterT
}

// MovieT object pack function
func (t *MovieT) Pack(builder *flatbuffers.Builder) flatbuffers.UOffsetT {
	if t == nil {
		return 0
	}
	mainCharacterOffset := t.MainCharacter.Pack(builder)
	
	// vector of unions 
	charactersOffset := flatbuffers.UOffsetT(0)
	charactersTypeOffset := flatbuffers.UOffsetT(0)
	if t.Characters != nil {
		charactersLength := len(t.Characters)
		charactersOffsets := make([]flatbuffers.UOffsetT, charactersLength)
		for j := charactersLength - 1; j >= 0; j-- {
			charactersOffsets[j] = t.Characters[j].Pack(builder)
		}
		MovieStartCharactersTypeVector(builder, charactersLength)
		for j := charactersLength - 1; j >= 0; j-- {
			builder.PrependByte(byte(t.Characters[j].Type))
		}
		charactersTypeOffset = MovieEndCharactersTypeVector(builder, charactersLength)
		MovieStartCharactersVector(builder, charactersLength)
		for j := charactersLength - 1; j >= 0; j-- {
			builder.PrependUOffsetT(charactersOffsets[j])
		}
		charactersOffset = MovieEndCharactersVector(builder, charactersLength)
	}

	// pack process all field

	MovieStart(builder)
	if t.MainCharacter != nil {
		MovieAddMainCharacterType(builder, t.MainCharacter.Type)
	}
	MovieAddMainCharacter(builder, mainCharacterOffset)
	MovieAddCharactersType(builder, charactersTypeOffset)
	MovieAddCharacters(builder, charactersOffset)
	return MovieEnd(builder)
}

// MovieT object unpack function
func (rcv *Movie) UnPackTo(t *MovieT) {
	mainCharacterTable := flatbuffers.Table{}
	if rcv.MainCharacter(&mainCharacterTable) {
		t.MainCharacter = rcv.MainCharacterType().UnPack(mainCharacterTable)
	}
	charactersLength := rcv.CharactersLength()
	t.Characters = make([]*CharacterT, charactersLength)
	for j := 0; j < charactersLength; j++ {
		// vector of unions table unpack
		CharactersType := rcv.CharactersType(j)
		CharactersTable := flatbuffers.Table{}
		if rcv.Characters(j, &CharactersTable) {
			t.Characters[j] = CharactersType.UnPackVector(CharactersTable)
		}
	}
}

func (rcv *Movie) UnPack() *MovieT {
	if rcv == nil {
		return nil
	}
	t := &MovieT{}
	rcv.UnPackTo(t)
	return t
}

type Movie struct {
	_tab flatbuffers.Table
}

// GetRootAsMovie shortcut to access root table
func GetRootAsMovie(buf []byte, offset flatbuffers.UOffsetT) *Movie {
	n := flatbuffers.GetUOffsetT(buf[offset:])
	x := &Movie{}
	x.Init(buf, n+offset)
	return x
}

// GetTableVectorAsMovie shortcut to access table in vector of  unions
func GetTableVectorAsMovie(table *flatbuffers.Table) *Movie {
	n := flatbuffers.GetUOffsetT(table.Bytes[table.Pos:])
	x := &Movie{}
	x.Init(table.Bytes, n+table.Pos)
	return x
}

// GetTableAsMovie shortcut to access table in single union field
func GetTableAsMovie(table *flatbuffers.Table) *Movie {
	x := &Movie{}
	x.Init(table.Bytes, table.Pos)
	return x
}

func (rcv *Movie) Init(buf []byte, i flatbuffers.UOffsetT) {
	rcv._tab.Bytes = buf
	rcv._tab.Pos = i
}

func (rcv *Movie) Table() flatbuffers.Table {
	return rcv._tab
}

func (rcv *Movie) MainCharacterType() Character {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(4))
	if o != 0 {
		return Character(rcv._tab.GetByte(o + rcv._tab.Pos))
	}
	return 0
}

func (rcv *Movie) MutateMainCharacterType(n Character) bool {
	return rcv._tab.MutateByteSlot(4, byte(n))
}

func (rcv *Movie) MainCharacter(obj *flatbuffers.Table) bool {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(6))
	if o != 0 {
		rcv._tab.Union(obj, o)
		return true
	}
	return false
}

func (rcv *Movie) CharactersType(j int) Character {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(8))
	if o != 0 {
		a := rcv._tab.Vector(o)
		return Character(rcv._tab.GetByte(a + flatbuffers.UOffsetT(j*1)))
	}
	return 0
}

func (rcv *Movie) CharactersTypeLength() int {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(8))
	if o != 0 {
		return rcv._tab.VectorLen(o)
	}
	return 0
}

func (rcv *Movie) MutateCharactersType(j int, n Character) bool {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(8))
	if o != 0 {
		a := rcv._tab.Vector(o)
		return rcv._tab.MutateByte(a+flatbuffers.UOffsetT(j*1), byte(n))
	}
	return false
}

func (rcv *Movie) Characters(j int, obj *flatbuffers.Table) bool {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(10))
	if o != 0 {
		a := rcv._tab.Vector(o)
		obj.Pos = a + flatbuffers.UOffsetT(j*4)
		obj.Bytes = rcv._tab.Bytes
		return true
	}
	return false
}

func (rcv *Movie) CharactersLength() int {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(10))
	if o != 0 {
		return rcv._tab.VectorLen(o)
	}
	return 0
}

func MovieStart(builder *flatbuffers.Builder) {
	builder.StartObject(4)
}

func MovieAddMainCharacterType(builder *flatbuffers.Builder, mainCharacterType Character) {
	builder.PrependByteSlot(0, byte(mainCharacterType), 0)
}

func MovieAddMainCharacter(builder *flatbuffers.Builder, mainCharacter flatbuffers.UOffsetT) {
	builder.PrependUOffsetTSlot(1, flatbuffers.UOffsetT(mainCharacter), 0)
}

func MovieAddCharactersType(builder *flatbuffers.Builder, charactersType flatbuffers.UOffsetT) {
	builder.PrependUOffsetTSlot(2, flatbuffers.UOffsetT(charactersType), 0)
}

func MovieStartCharactersTypeVector(builder *flatbuffers.Builder, numElems int) flatbuffers.UOffsetT {
	return builder.StartVector(1, numElems, 1)
}

func MovieEndCharactersTypeVector(builder *flatbuffers.Builder, numElems int) flatbuffers.UOffsetT {
	return builder.EndVector(numElems)
}

func MovieAddCharacters(builder *flatbuffers.Builder, characters flatbuffers.UOffsetT) {
	builder.PrependUOffsetTSlot(3, flatbuffers.UOffsetT(characters), 0)
}

func MovieStartCharactersVector(builder *flatbuffers.Builder, numElems int) flatbuffers.UOffsetT {
	return builder.StartVector(4, numElems, 4)
}

func MovieEndCharactersVector(builder *flatbuffers.Builder, numElems int) flatbuffers.UOffsetT {
	return builder.EndVector(numElems)
}

func MovieEnd(builder *flatbuffers.Builder) flatbuffers.UOffsetT {
	return builder.EndObject()
}
