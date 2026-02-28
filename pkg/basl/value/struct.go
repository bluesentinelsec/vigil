package value

import (
	"encoding/binary"
	"fmt"
	"math"
	"unsafe"
)

// Get reads field at index from the struct buffer.
func (s *StructVal) Get(idx int) (Value, error) {
	if idx < 0 || idx >= len(s.Layout.Fields) {
		return Void, fmt.Errorf("unsafe.Struct: field index %d out of range", idx)
	}
	f := s.Layout.Fields[idx]
	b := s.Buf[f.Offset : f.Offset+f.Size]
	switch f.Type {
	case "i32":
		return NewI32(int32(binary.LittleEndian.Uint32(b))), nil
	case "u32":
		return NewU32(binary.LittleEndian.Uint32(b)), nil
	case "f32":
		bits := binary.LittleEndian.Uint32(b)
		return NewF64(float64(math.Float32frombits(bits))), nil
	case "u8":
		return NewU8(b[0]), nil
	case "i64":
		return NewI64(int64(binary.LittleEndian.Uint64(b))), nil
	case "u64":
		return NewU64(binary.LittleEndian.Uint64(b)), nil
	case "f64":
		bits := binary.LittleEndian.Uint64(b)
		return NewF64(*(*float64)(unsafe.Pointer(&bits))), nil
	}
	return Void, fmt.Errorf("unsafe.Struct: unsupported field type %q", f.Type)
}

// Set writes a value to field at index in the struct buffer.
func (s *StructVal) Set(idx int, v Value) error {
	if idx < 0 || idx >= len(s.Layout.Fields) {
		return fmt.Errorf("unsafe.Struct: field index %d out of range", idx)
	}
	f := s.Layout.Fields[idx]
	b := s.Buf[f.Offset : f.Offset+f.Size]
	switch f.Type {
	case "i32":
		binary.LittleEndian.PutUint32(b, uint32(v.AsI32()))
	case "u32":
		binary.LittleEndian.PutUint32(b, v.AsU32())
	case "f32":
		binary.LittleEndian.PutUint32(b, math.Float32bits(float32(v.AsF64())))
	case "u8":
		b[0] = v.AsU8()
	case "i64":
		binary.LittleEndian.PutUint64(b, uint64(v.AsI64()))
	case "u64":
		binary.LittleEndian.PutUint64(b, v.AsU64())
	case "f64":
		bits := *(*uint64)(unsafe.Pointer(&v.Data))
		binary.LittleEndian.PutUint64(b, bits)
	default:
		return fmt.Errorf("unsafe.Struct: unsupported field type %q", f.Type)
	}
	return nil
}

// Ptr returns a pointer to the backing buffer, suitable for passing to C.
func (s *StructVal) Ptr() uintptr {
	return uintptr(unsafe.Pointer(&s.Buf[0]))
}
