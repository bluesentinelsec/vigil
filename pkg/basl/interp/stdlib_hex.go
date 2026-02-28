package interp

import (
	"encoding/hex"
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// --- Hex ---

func (interp *Interpreter) makeHexModule() *Env {
	env := NewEnv(nil)
	env.Define("encode", value.NewNativeFunc("hex.encode", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("hex.encode: expected string")
		}
		return value.NewString(hex.EncodeToString([]byte(args[0].AsString()))), nil
	}))
	env.Define("decode", value.NewNativeFunc("hex.decode", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("hex.decode: expected string")
		}
		b, err := hex.DecodeString(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(b)), value.Ok}}
	}))
	return env
}
