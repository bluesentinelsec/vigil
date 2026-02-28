package interp

import (
	"encoding/binary"
	"fmt"
	gounsafe "unsafe"

	"github.com/bluesentinelsec/basl/pkg/basl/ffi"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeUnsafeModule() *Env {
	env := NewEnv(nil)
	env.Define("null", value.NullPtr)

	// unsafe.callback(fn, string ret_type, ...string param_types) -> unsafe.Callback
	env.Define("callback", value.NewNativeFunc("unsafe.callback", func(args []value.Value) (value.Value, error) {
		if len(args) < 2 {
			return value.Void, fmt.Errorf("unsafe.callback: expected (fn, ret_type, ...param_types)")
		}
		if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
			return value.Void, fmt.Errorf("unsafe.callback: first arg must be a function")
		}
		if args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("unsafe.callback: second arg must be string return type")
		}

		baslFn := args[0]
		nParams := len(args) - 2

		// Register a Go callback that invokes the BASL function
		goFn := func(cArgs []uintptr) uintptr {
			baslArgs := make([]value.Value, nParams)
			for i := 0; i < nParams && i < len(cArgs); i++ {
				baslArgs[i] = value.NewPtr(cArgs[i])
			}
			result, _ := interp.callFunc(baslFn, baslArgs)
			switch result.T {
			case value.TypeI32:
				return uintptr(result.AsI32())
			case value.TypePtr:
				return result.AsPtr()
			default:
				return 0
			}
		}

		ptr, slot, err := ffi.RegisterCallback(goFn)
		if err != nil {
			return value.Void, fmt.Errorf("%s", err.Error())
		}

		return interp.wrapCallback(ptr, slot), nil
	}))

	// unsafe.alloc(i32 size) -> unsafe.Buffer
	env.Define("alloc", value.NewNativeFunc("unsafe.alloc", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("unsafe.alloc: expected i32 size")
		}
		size := int(args[0].AsI32())
		if size <= 0 {
			return value.Void, fmt.Errorf("unsafe.alloc: size must be > 0")
		}
		buf := &value.BufferVal{Data: make([]byte, size)}
		return interp.wrapBuffer(buf), nil
	}))

	// unsafe.layout(string types...) -> unsafe.Layout descriptor
	env.Define("layout", value.NewNativeFunc("unsafe.layout", func(args []value.Value) (value.Value, error) {
		fields := make([]value.StructFieldDef, len(args))
		offset := 0
		for i, a := range args {
			if a.T != value.TypeString {
				return value.Void, fmt.Errorf("unsafe.layout: arg %d must be string type name", i)
			}
			typeName := a.AsString()
			size := fieldSize(typeName)
			if size == 0 {
				return value.Void, fmt.Errorf("unsafe.layout: unsupported type %q", typeName)
			}
			fields[i] = value.StructFieldDef{Type: typeName, Offset: offset, Size: size}
			offset += size
		}
		layout := &value.StructLayout{Fields: fields, TotalSize: offset}
		return interp.wrapLayout(layout), nil
	}))

	return env
}

func fieldSize(typeName string) int {
	switch typeName {
	case "i32", "u32", "f32":
		return 4
	case "i64", "u64", "f64":
		return 8
	case "u8":
		return 1
	case "ptr":
		return 8 // 64-bit pointers
	}
	return 0
}

func (interp *Interpreter) wrapLayout(layout *value.StructLayout) value.Value {
	obj := &value.ObjectVal{
		ClassName: "unsafe.Layout",
		Fields:    make(map[string]value.Value),
		Methods:   make(map[string]*value.FuncVal),
	}

	// layout.new() -> unsafe.Struct
	obj.Fields["new"] = value.NewNativeFunc("unsafe.Layout.new", func(args []value.Value) (value.Value, error) {
		s := value.NewStruct(layout)
		return interp.wrapStruct(s), nil
	})

	return value.Value{T: value.TypeObject, Data: obj}
}

func (interp *Interpreter) wrapStruct(s *value.StructVal) value.Value {
	obj := &value.ObjectVal{
		ClassName: "unsafe.Struct",
		Fields:    make(map[string]value.Value),
		Methods:   make(map[string]*value.FuncVal),
	}

	// struct.get(i32 index) -> value
	obj.Fields["get"] = value.NewNativeFunc("unsafe.Struct.get", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("unsafe.Struct.get: expected i32 index")
		}
		return s.Get(int(args[0].AsI32()))
	})

	// struct.set(i32 index, value)
	obj.Fields["set"] = value.NewNativeFunc("unsafe.Struct.set", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("unsafe.Struct.set: expected (i32 index, value)")
		}
		return value.Void, s.Set(int(args[0].AsI32()), args[1])
	})

	// struct.ptr() -> unsafe.Ptr
	obj.Fields["ptr"] = value.NewNativeFunc("unsafe.Struct.ptr", func(args []value.Value) (value.Value, error) {
		return value.NewPtr(s.Ptr()), nil
	})

	return value.Value{T: value.TypeObject, Data: obj}
}

func (interp *Interpreter) wrapCallback(ptr gounsafe.Pointer, slot int) value.Value {
	obj := &value.ObjectVal{
		ClassName: "unsafe.Callback",
		Fields:    make(map[string]value.Value),
		Methods:   make(map[string]*value.FuncVal),
	}

	// cb.ptr() -> unsafe.Ptr (the C function pointer)
	obj.Fields["ptr"] = value.NewNativeFunc("unsafe.Callback.ptr", func(args []value.Value) (value.Value, error) {
		return value.NewPtr(uintptr(ptr)), nil
	})

	// cb.free() — release the callback slot
	obj.Fields["free"] = value.NewNativeFunc("unsafe.Callback.free", func(args []value.Value) (value.Value, error) {
		ffi.FreeCallback(slot)
		return value.Ok, nil
	})

	return value.Value{T: value.TypeObject, Data: obj}
}

func (interp *Interpreter) wrapBuffer(buf *value.BufferVal) value.Value {
	obj := &value.ObjectVal{
		ClassName: "unsafe.Buffer",
		Fields:    make(map[string]value.Value),
		Methods:   make(map[string]*value.FuncVal),
	}

	// buf.ptr() -> unsafe.Ptr
	obj.Fields["ptr"] = value.NewNativeFunc("unsafe.Buffer.ptr", func(args []value.Value) (value.Value, error) {
		return value.NewPtr(uintptr(gounsafe.Pointer(&buf.Data[0]))), nil
	})

	// buf.len() -> i32
	obj.Fields["len"] = value.NewNativeFunc("unsafe.Buffer.len", func(args []value.Value) (value.Value, error) {
		return value.NewI32(int32(len(buf.Data))), nil
	})

	// buf.get(i32 index) -> u8
	obj.Fields["get"] = value.NewNativeFunc("unsafe.Buffer.get", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("unsafe.Buffer.get: expected i32 index")
		}
		i := int(args[0].AsI32())
		if i < 0 || i >= len(buf.Data) {
			return value.Void, fmt.Errorf("unsafe.Buffer.get: index %d out of bounds", i)
		}
		return value.NewU8(buf.Data[i]), nil
	})

	// buf.set(i32 index, u8 value)
	obj.Fields["set"] = value.NewNativeFunc("unsafe.Buffer.set", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("unsafe.Buffer.set: expected (i32 index, u8 value)")
		}
		i := int(args[0].AsI32())
		if i < 0 || i >= len(buf.Data) {
			return value.Void, fmt.Errorf("unsafe.Buffer.set: index %d out of bounds", i)
		}
		switch args[1].T {
		case value.TypeU8:
			buf.Data[i] = args[1].AsU8()
		case value.TypeI32:
			buf.Data[i] = byte(args[1].AsI32())
		default:
			return value.Void, fmt.Errorf("unsafe.Buffer.set: value must be u8 or i32")
		}
		return value.Void, nil
	})

	// buf.get_u32(i32 byte_offset) -> u32
	obj.Fields["get_u32"] = value.NewNativeFunc("unsafe.Buffer.get_u32", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("unsafe.Buffer.get_u32: expected i32 offset")
		}
		off := int(args[0].AsI32())
		if off < 0 || off+4 > len(buf.Data) {
			return value.Void, fmt.Errorf("unsafe.Buffer.get_u32: offset %d out of bounds", off)
		}
		v := binary.LittleEndian.Uint32(buf.Data[off : off+4])
		return value.NewU32(v), nil
	})

	return value.Value{T: value.TypeObject, Data: obj}
}
