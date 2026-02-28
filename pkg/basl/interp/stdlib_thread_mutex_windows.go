//go:build windows

package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// Windows stub - threads and mutexes not yet implemented on Windows

func (interp *Interpreter) makeThreadModule() *Env {
	env := NewEnv(nil)
	env.Define("spawn", value.NewNativeFunc("thread.spawn", func(args []value.Value) (value.Value, error) {
		return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("thread module not supported on Windows yet")}}
	}))
	env.Define("sleep", value.NewNativeFunc("thread.sleep", func(args []value.Value) (value.Value, error) {
		return value.NewErr("thread module not supported on Windows yet"), nil
	}))
	return env
}

func (interp *Interpreter) threadMethod(obj value.Value, method string, line int) (value.Value, error) {
	return value.Void, fmt.Errorf("line %d: thread module not supported on Windows yet", line)
}

func (interp *Interpreter) makeMutexModule() *Env {
	env := NewEnv(nil)
	env.Define("new", value.NewNativeFunc("mutex.new", func(args []value.Value) (value.Value, error) {
		return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("mutex module not supported on Windows yet")}}
	}))
	return env
}

func (interp *Interpreter) mutexMethod(obj value.Value, method string, line int) (value.Value, error) {
	return value.Void, fmt.Errorf("line %d: mutex module not supported on Windows yet", line)
}
