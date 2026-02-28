package interp

import (
	"encoding/base64"
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// --- Base64 ---

func (interp *Interpreter) makeBase64Module() *Env {
	env := NewEnv(nil)
	env.Define("encode", value.NewNativeFunc("base64.encode", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("base64.encode: expected string")
		}
		return value.NewString(base64.StdEncoding.EncodeToString([]byte(args[0].AsString()))), nil
	}))
	env.Define("decode", value.NewNativeFunc("base64.decode", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("base64.decode: expected string")
		}
		b, err := base64.StdEncoding.DecodeString(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(b)), value.Ok}}
	}))
	return env
}
