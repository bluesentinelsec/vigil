package interp

import (
	"fmt"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeStringsModule() *Env {
	env := NewEnv(nil)
	env.Define("join", value.NewNativeFunc("strings.join", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeArray || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("strings.join: expected (array<string>, string sep)")
		}
		arr := args[0].AsArray()
		parts := make([]string, len(arr.Elems))
		for i, e := range arr.Elems {
			parts[i] = e.String()
		}
		return value.NewString(strings.Join(parts, args[1].AsString())), nil
	}))
	env.Define("repeat", value.NewNativeFunc("strings.repeat", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeI32 {
			return value.Void, fmt.Errorf("strings.repeat: expected (string, i32)")
		}
		return value.NewString(strings.Repeat(args[0].AsString(), int(args[1].AsI32()))), nil
	}))
	return env
}
