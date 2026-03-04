package interp

import (
	"fmt"
	"strconv"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeParseModule() *Env {
	env := NewEnv(nil)
	env.Define("i32", value.NewNativeFunc("parse.i32", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("parse.i32: expected string")
		}
		n, err := strconv.ParseInt(args[0].AsString(), 10, 32)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(0), value.NewErr("invalid i32: "+args[0].AsString(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(int32(n)), value.Ok}}
	}))
	env.Define("i64", value.NewNativeFunc("parse.i64", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("parse.i64: expected string")
		}
		n, err := strconv.ParseInt(args[0].AsString(), 10, 64)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI64(0), value.NewErr("invalid i64: "+args[0].AsString(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI64(n), value.Ok}}
	}))
	env.Define("f64", value.NewNativeFunc("parse.f64", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("parse.f64: expected string")
		}
		n, err := strconv.ParseFloat(args[0].AsString(), 64)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(0), value.NewErr("invalid f64: "+args[0].AsString(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(n), value.Ok}}
	}))
	env.Define("u8", value.NewNativeFunc("parse.u8", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("parse.u8: expected string")
		}
		n, err := strconv.ParseUint(args[0].AsString(), 10, 8)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewU8(0), value.NewErr("invalid u8: "+args[0].AsString(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewU8(uint8(n)), value.Ok}}
	}))
	env.Define("u32", value.NewNativeFunc("parse.u32", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("parse.u32: expected string")
		}
		n, err := strconv.ParseUint(args[0].AsString(), 10, 32)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewU32(0), value.NewErr("invalid u32: "+args[0].AsString(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewU32(uint32(n)), value.Ok}}
	}))
	env.Define("u64", value.NewNativeFunc("parse.u64", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("parse.u64: expected string")
		}
		n, err := strconv.ParseUint(args[0].AsString(), 10, 64)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewU64(0), value.NewErr("invalid u64: "+args[0].AsString(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewU64(n), value.Ok}}
	}))
	env.Define("bool", value.NewNativeFunc("parse.bool", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("parse.bool: expected string")
		}
		switch args[0].AsString() {
		case "true":
			return value.Void, &MultiReturnVal{Values: []value.Value{value.True, value.Ok}}
		case "false":
			return value.Void, &MultiReturnVal{Values: []value.Value{value.False, value.Ok}}
		default:
			return value.Void, &MultiReturnVal{Values: []value.Value{value.False, value.NewErr("invalid bool: "+args[0].AsString(), value.ErrKindParse)}}
		}
	}))
	return env
}
