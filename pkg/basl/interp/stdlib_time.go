package interp

import (
	"fmt"
	"time"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeTimeModule() *Env {
	env := NewEnv(nil)
	env.Define("now", value.NewNativeFunc("time.now", func(args []value.Value) (value.Value, error) {
		return value.NewI64(time.Now().UnixMilli()), nil
	}))
	env.Define("sleep", value.NewNativeFunc("time.sleep", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("time.sleep: expected i32 milliseconds")
		}
		time.Sleep(time.Duration(args[0].AsI32()) * time.Millisecond)
		return value.Void, nil
	}))
	env.Define("since", value.NewNativeFunc("time.since", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI64 {
			return value.Void, fmt.Errorf("time.since: expected i64 epoch_millis")
		}
		elapsed := time.Now().UnixMilli() - args[0].AsI64()
		return value.NewI64(elapsed), nil
	}))
	env.Define("format", value.NewNativeFunc("time.format", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeI64 || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("time.format: expected (i64 epoch_millis, string layout)")
		}
		t := time.UnixMilli(args[0].AsI64())
		return value.NewString(t.Format(args[1].AsString())), nil
	}))
	env.Define("parse", value.NewNativeFunc("time.parse", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("time.parse: expected (string layout, string value)")
		}
		t, err := time.Parse(args[0].AsString(), args[1].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI64(0), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI64(t.UnixMilli()), value.Ok}}
	}))
	return env
}
