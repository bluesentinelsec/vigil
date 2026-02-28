package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/ffi"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeFfiModule() *Env {
	env := NewEnv(nil)

	// ffi.load(string path) -> (ffi.Lib, err)
	env.Define("load", value.NewNativeFunc("ffi.load", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("ffi.load: expected string path")
		}
		lib, err := ffi.Open(args[0].AsString(), interp.ffiPolicy)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.wrapFfiLib(lib), value.Ok}}
	}))

	// ffi.bind(ffi.Lib lib, string name, string ret_type, ...string param_types) -> (ffi.Func, err)
	env.Define("bind", value.NewNativeFunc("ffi.bind", func(args []value.Value) (value.Value, error) {
		if len(args) < 3 {
			return value.Void, fmt.Errorf("ffi.bind: expected (lib, name, ret_type, ...param_types)")
		}
		if args[0].T != value.TypeObject {
			return value.Void, fmt.Errorf("ffi.bind: first arg must be ffi.Lib")
		}
		if args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("ffi.bind: second arg must be string name")
		}
		if args[2].T != value.TypeString {
			return value.Void, fmt.Errorf("ffi.bind: third arg must be string return type")
		}

		obj := args[0].AsObject()
		libField, ok := obj.Fields["__lib"]
		if !ok {
			return value.Void, fmt.Errorf("ffi.bind: first arg is not an ffi.Lib")
		}
		lib := libField.Data.(*ffi.Lib)
		symName := args[1].AsString()
		retType := args[2].AsString()

		var paramTypes []string
		for i := 3; i < len(args); i++ {
			if args[i].T != value.TypeString {
				return value.Void, fmt.Errorf("ffi.bind: param type %d must be string", i-2)
			}
			paramTypes = append(paramTypes, args[i].AsString())
		}

		bound, err := lib.Bind(symName, retType, paramTypes)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.wrapBoundFunc(bound), value.Ok}}
	}))

	return env
}

func (interp *Interpreter) wrapFfiLib(lib *ffi.Lib) value.Value {
	obj := &value.ObjectVal{
		ClassName: "ffi.Lib",
		Fields:    make(map[string]value.Value),
		Methods:   make(map[string]*value.FuncVal),
	}
	// Stash the Go *ffi.Lib so ffi.bind() can retrieve it.
	obj.Fields["__lib"] = value.Value{T: value.TypeNativeFunc, Data: lib}

	obj.Fields["close"] = value.NewNativeFunc("ffi.Lib.close", func(args []value.Value) (value.Value, error) {
		if err := lib.Close(); err != nil {
			return value.NewErr(err.Error()), nil
		}
		return value.Ok, nil
	})

	return value.Value{T: value.TypeObject, Data: obj}
}

func (interp *Interpreter) wrapBoundFunc(bf *ffi.BoundFunc) value.Value {
	obj := &value.ObjectVal{
		ClassName: "ffi.Func",
		Fields:    make(map[string]value.Value),
		Methods:   make(map[string]*value.FuncVal),
	}

	obj.Fields["call"] = value.NewNativeFunc("ffi.Func.call", func(args []value.Value) (value.Value, error) {
		// Check if signature involves ptr — use generic trampoline
		hasPtr := bf.RetType() == "ptr"
		for _, pt := range bf.ParamTypes() {
			if pt == "ptr" {
				hasPtr = true
			}
		}

		if hasPtr {
			return callGeneric(bf, args)
		}

		// Marshal BASL values to Go interface{} for ffi.BoundFunc.Call
		goArgs := make([]interface{}, len(args))
		for i, a := range args {
			switch a.T {
			case value.TypeI32:
				goArgs[i] = a.AsI32()
			case value.TypeI64:
				goArgs[i] = a.AsI64()
			case value.TypeF64:
				goArgs[i] = a.AsF64()
			case value.TypeU8:
				goArgs[i] = int32(a.AsU8())
			case value.TypeU32:
				goArgs[i] = int32(a.AsU32())
			case value.TypeU64:
				goArgs[i] = int64(a.AsU64())
			case value.TypeString:
				goArgs[i] = a.AsString()
			default:
				return value.Void, fmt.Errorf("ffi.Func.call: unsupported arg type at position %d", i)
			}
		}

		result, err := bf.Call(goArgs)
		if err != nil {
			def := defaultForType(bf.RetType())
			return value.Void, &MultiReturnVal{Values: []value.Value{def, value.NewErr(err.Error())}}
		}

		retVal := marshalReturn(result, bf.RetType())
		if bf.RetType() == "void" {
			return value.Ok, nil
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{retVal, value.Ok}}
	})

	return value.Value{T: value.TypeObject, Data: obj}
}

func callGeneric(bf *ffi.BoundFunc, args []value.Value) (value.Value, error) {
	goArgs := make([]interface{}, len(args))
	for i, a := range args {
		switch a.T {
		case value.TypePtr:
			goArgs[i] = a.AsPtr()
		case value.TypeI32:
			goArgs[i] = int32(a.AsI32())
		case value.TypeU32:
			goArgs[i] = uint32(a.AsU32())
		case value.TypeU8:
			goArgs[i] = uint32(a.AsU8())
		case value.TypeString:
			goArgs[i] = a.AsString()
		default:
			return value.Void, fmt.Errorf("ffi.Func.call: unsupported arg type at position %d", i)
		}
	}

	result, err := bf.CallGeneric(goArgs)
	if err != nil {
		def := defaultForType(bf.RetType())
		return value.Void, &MultiReturnVal{Values: []value.Value{def, value.NewErr(err.Error())}}
	}

	switch bf.RetType() {
	case "ptr":
		if result == 0 {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NullPtr, value.NewErr("returned null")}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewPtr(result), value.Ok}}
	case "i32":
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(int32(result)), value.Ok}}
	case "void":
		return value.Ok, nil
	default:
		return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.Ok}}
	}
}

func defaultForType(t string) value.Value {
	switch t {
	case "i32":
		return value.NewI32(0)
	case "f64":
		return value.NewF64(0)
	case "string":
		return value.NewString("")
	default:
		return value.Void
	}
}

func marshalReturn(result interface{}, retType string) value.Value {
	if result == nil {
		return value.Void
	}
	switch retType {
	case "i32":
		return value.NewI32(result.(int32))
	case "f64":
		return value.NewF64(result.(float64))
	case "string":
		return value.NewString(result.(string))
	default:
		return value.Void
	}
}
