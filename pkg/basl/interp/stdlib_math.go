package interp

import (
	"fmt"
	"math"
	"math/rand"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeMathModule() *Env {
	env := NewEnv(nil)
	f64Unary := func(name string, fn func(float64) float64) value.Value {
		return value.NewNativeFunc(name, func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeF64 {
				return value.Void, fmt.Errorf("%s: expected f64 argument", name)
			}
			return value.NewF64(fn(args[0].AsF64())), nil
		})
	}
	env.Define("sqrt", f64Unary("math.sqrt", math.Sqrt))
	env.Define("abs", f64Unary("math.abs", math.Abs))
	env.Define("floor", f64Unary("math.floor", math.Floor))
	env.Define("abs", f64Unary("math.abs", math.Abs))
	env.Define("ceil", f64Unary("math.ceil", math.Ceil))
	env.Define("round", f64Unary("math.round", math.Round))
	env.Define("sin", f64Unary("math.sin", math.Sin))
	env.Define("cos", f64Unary("math.cos", math.Cos))
	env.Define("tan", f64Unary("math.tan", math.Tan))
	env.Define("log", f64Unary("math.log", math.Log))
	env.Define("pow", value.NewNativeFunc("math.pow", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeF64 || args[1].T != value.TypeF64 {
			return value.Void, fmt.Errorf("math.pow: expected (f64, f64)")
		}
		return value.NewF64(math.Pow(args[0].AsF64(), args[1].AsF64())), nil
	}))
	env.Define("min", value.NewNativeFunc("math.min", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeF64 || args[1].T != value.TypeF64 {
			return value.Void, fmt.Errorf("math.min: expected (f64, f64)")
		}
		return value.NewF64(math.Min(args[0].AsF64(), args[1].AsF64())), nil
	}))
	env.Define("max", value.NewNativeFunc("math.max", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeF64 || args[1].T != value.TypeF64 {
			return value.Void, fmt.Errorf("math.max: expected (f64, f64)")
		}
		return value.NewF64(math.Max(args[0].AsF64(), args[1].AsF64())), nil
	}))
	env.Define("pi", value.NewF64(math.Pi))
	env.Define("e", value.NewF64(math.E))
	env.Define("random", value.NewNativeFunc("math.random", func(args []value.Value) (value.Value, error) {
		return value.NewF64(rand.Float64()), nil
	}))
	return env
}
