package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// signalTestFail is used to abort a test function on t.fail / t.assert.
type signalTestFail struct {
	Message string
}

func (s *signalTestFail) Error() string { return s.Message }

func (interp *Interpreter) makeTestModule() *Env {
	env := NewEnv(nil)

	// Create test.T type with assert and fail methods
	env.Define("T", value.NewNativeFunc("test.T", func(args []value.Value) (value.Value, error) {
		// Constructor - returns a test.T object
		obj := &value.ObjectVal{
			ClassName: "test.T",
			Fields:    map[string]value.Value{},
		}
		return value.Value{T: value.TypeObject, Data: obj}, nil
	}))

	// Legacy functions for backward compatibility
	env.Define("assert", value.NewNativeFunc("t.assert", func(args []value.Value) (value.Value, error) {
		if len(args) < 1 || args[0].T != value.TypeBool {
			return value.Void, fmt.Errorf("t.assert: expected bool condition")
		}
		if !args[0].AsBool() {
			msg := "assertion failed"
			if len(args) >= 2 && args[1].T == value.TypeString {
				msg = args[1].AsString()
			}
			return value.Void, &signalTestFail{Message: msg}
		}
		return value.Void, nil
	}))

	env.Define("fail", value.NewNativeFunc("t.fail", func(args []value.Value) (value.Value, error) {
		msg := "test failed"
		if len(args) >= 1 && args[0].T == value.TypeString {
			msg = args[0].AsString()
		}
		return value.Void, &signalTestFail{Message: msg}
	}))

	return env
}

// testTMethod handles methods on test.T objects
func (interp *Interpreter) testTMethod(obj value.Value, method string, line int) (value.Value, error) {
	switch method {
	case "assert":
		return value.NewNativeFunc("T.assert", func(args []value.Value) (value.Value, error) {
			if len(args) < 1 || args[0].T != value.TypeBool {
				return value.Void, fmt.Errorf("T.assert: expected bool condition")
			}
			if !args[0].AsBool() {
				msg := "assertion failed"
				if len(args) >= 2 && args[1].T == value.TypeString {
					msg = args[1].AsString()
				}
				return value.Void, &signalTestFail{Message: msg}
			}
			return value.Void, nil
		}), nil
	case "fail":
		return value.NewNativeFunc("T.fail", func(args []value.Value) (value.Value, error) {
			msg := "test failed"
			if len(args) >= 1 && args[0].T == value.TypeString {
				msg = args[0].AsString()
			}
			return value.Void, &signalTestFail{Message: msg}
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: test.T has no method '%s'", line, method)
	}
}
