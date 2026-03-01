package interp

import (
	"fmt"
	"os"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeFmtModule() *Env {
	env := NewEnv(nil)
	env.Define("print", value.NewNativeFunc("fmt.print", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 {
			return value.Void, fmt.Errorf("fmt.print: expected 1 argument, got %d", len(args))
		}
		interp.PrintFn(args[0].String())
		return value.Ok, nil
	}))
	env.Define("println", value.NewNativeFunc("fmt.println", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 {
			return value.Void, fmt.Errorf("fmt.println: expected 1 argument, got %d", len(args))
		}
		interp.PrintFn(args[0].String() + "\n")
		return value.Ok, nil
	}))
	env.Define("sprintf", value.NewNativeFunc("fmt.sprintf", func(args []value.Value) (value.Value, error) {
		if len(args) < 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("fmt.sprintf: expected (string format, ...args)")
		}
		fmtStr := args[0].AsString()
		goArgs := make([]interface{}, len(args)-1)
		for i := 1; i < len(args); i++ {
			goArgs[i-1] = args[i].Data
		}
		return value.NewString(fmt.Sprintf(fmtStr, goArgs...)), nil
	}))
	env.Define("dollar", value.NewNativeFunc("fmt.dollar", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 {
			return value.Void, fmt.Errorf("fmt.dollar: expected 1 argument, got %d", len(args))
		}
		switch args[0].T {
		case value.TypeF64:
			return value.NewString(fmt.Sprintf("$%.2f", args[0].AsF64())), nil
		case value.TypeI32:
			return value.NewString(fmt.Sprintf("$%d.00", args[0].AsI32())), nil
		case value.TypeI64:
			return value.NewString(fmt.Sprintf("$%d.00", args[0].AsI64())), nil
		default:
			return value.Void, fmt.Errorf("fmt.dollar: expected numeric type, received %s", args[0].T.String())
		}
	}))
	env.Define("eprint", value.NewNativeFunc("fmt.eprint", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 {
			return value.Void, fmt.Errorf("fmt.eprint: expected 1 argument, got %d", len(args))
		}
		fmt.Fprint(os.Stderr, args[0].String())
		return value.Ok, nil
	}))
	env.Define("eprintln", value.NewNativeFunc("fmt.eprintln", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 {
			return value.Void, fmt.Errorf("fmt.eprintln: expected 1 argument, got %d", len(args))
		}
		fmt.Fprintln(os.Stderr, args[0].String())
		return value.Ok, nil
	}))
	return env
}
