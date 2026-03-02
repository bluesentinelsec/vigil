package interp

import (
	"fmt"
	"path/filepath"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makePathModule() *Env {
	env := NewEnv(nil)
	env.Define("join", value.NewNativeFunc("path.join", func(args []value.Value) (value.Value, error) {
		parts := make([]string, len(args))
		for i, a := range args {
			if a.T != value.TypeString {
				return value.Void, fmt.Errorf("path.join: arg %d must be string", i)
			}
			parts[i] = a.AsString()
		}
		return value.NewString(filepath.Join(parts...)), nil
	}))
	env.Define("dir", value.NewNativeFunc("path.dir", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("path.dir: expected string")
		}
		return value.NewString(filepath.Dir(args[0].AsString())), nil
	}))
	env.Define("base", value.NewNativeFunc("path.base", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("path.base: expected string")
		}
		return value.NewString(filepath.Base(args[0].AsString())), nil
	}))
	env.Define("ext", value.NewNativeFunc("path.ext", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("path.ext: expected string")
		}
		return value.NewString(filepath.Ext(args[0].AsString())), nil
	}))
	env.Define("abs", value.NewNativeFunc("path.abs", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("path.abs: expected string")
		}
		p, err := filepath.Abs(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(p), value.Ok}}
	}))
	return env
}
