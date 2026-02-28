package interp

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io"
	"math/big"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeRandModule() *Env {
	env := NewEnv(nil)
	env.Define("bytes", value.NewNativeFunc("rand.bytes", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("rand.bytes: expected i32 count")
		}
		buf := make([]byte, args[0].AsI32())
		if _, err := io.ReadFull(rand.Reader, buf); err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		return value.NewString(hex.EncodeToString(buf)), nil
	}))
	env.Define("int", value.NewNativeFunc("rand.int", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeI32 || args[1].T != value.TypeI32 {
			return value.Void, fmt.Errorf("rand.int: expected (i32 min, i32 max)")
		}
		lo := int64(args[0].AsI32())
		hi := int64(args[1].AsI32())
		if lo >= hi {
			return value.Void, fmt.Errorf("rand.int: min must be < max")
		}
		n, err := rand.Int(rand.Reader, big.NewInt(hi-lo))
		if err != nil {
			return value.Void, fmt.Errorf("rand.int: %s", err.Error())
		}
		return value.NewI32(int32(n.Int64() + lo)), nil
	}))
	return env
}
