//go:build windows

package interp

import "github.com/bluesentinelsec/basl/pkg/basl/value"

// Windows stub - threads and mutexes not yet implemented on Windows

func registerThreadModule(i *Interpreter) {
	i.modules["thread"] = map[string]value.Value{
		"spawn": value.NativeFunc(func(args []value.Value) (value.Value, error) {
			return value.Err("thread module not supported on Windows yet"), nil
		}),
		"join": value.NativeFunc(func(args []value.Value) (value.Value, error) {
			return value.Err("thread module not supported on Windows yet"), nil
		}),
		"sleep": value.NativeFunc(func(args []value.Value) (value.Value, error) {
			return value.Err("thread module not supported on Windows yet"), nil
		}),
	}
}

func registerMutexModule(i *Interpreter) {
	i.modules["mutex"] = map[string]value.Value{
		"new": value.NativeFunc(func(args []value.Value) (value.Value, error) {
			return value.Err("mutex module not supported on Windows yet"), nil
		}),
		"lock": value.NativeFunc(func(args []value.Value) (value.Value, error) {
			return value.Err("mutex module not supported on Windows yet"), nil
		}),
		"unlock": value.NativeFunc(func(args []value.Value) (value.Value, error) {
			return value.Err("mutex module not supported on Windows yet"), nil
		}),
		"destroy": value.NativeFunc(func(args []value.Value) (value.Value, error) {
			return value.Err("mutex module not supported on Windows yet"), nil
		}),
	}
}
