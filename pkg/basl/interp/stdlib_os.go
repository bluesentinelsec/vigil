package interp

import (
	"fmt"
	"os"
	"os/exec"
	"runtime"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeOsModule() *Env {
	env := NewEnv(nil)
	env.Define("args", value.NewNativeFunc("os.args", func(args []value.Value) (value.Value, error) {
		return interp.GetScriptArgs(), nil
	}))
	env.Define("env", value.NewNativeFunc("os.env", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("os.env: expected string key")
		}
		v, ok := os.LookupEnv(args[0].AsString())
		if !ok {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.False}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(v), value.True}}
	}))
	env.Define("set_env", value.NewNativeFunc("os.set_env", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("os.set_env: expected (string key, string value)")
		}
		if err := os.Setenv(args[0].AsString(), args[1].AsString()); err != nil {
			return value.NewErr(err.Error()), nil
		}
		return value.Ok, nil
	}))
	env.Define("exit", value.NewNativeFunc("os.exit", func(args []value.Value) (value.Value, error) {
		code := 0
		if len(args) == 1 && args[0].T == value.TypeI32 {
			code = int(args[0].AsI32())
		}
		os.Exit(code)
		return value.Void, nil
	}))
	env.Define("cwd", value.NewNativeFunc("os.cwd", func(args []value.Value) (value.Value, error) {
		dir, err := os.Getwd()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(dir), value.Ok}}
	}))
	env.Define("hostname", value.NewNativeFunc("os.hostname", func(args []value.Value) (value.Value, error) {
		h, err := os.Hostname()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(h), value.Ok}}
	}))
	env.Define("platform", value.NewNativeFunc("os.platform", func(args []value.Value) (value.Value, error) {
		return value.NewString(runtime.GOOS), nil
	}))
	env.Define("exec", value.NewNativeFunc("os.exec", func(args []value.Value) (value.Value, error) {
		if len(args) < 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("os.exec: expected (string cmd, ...string args)")
		}
		cmdName := args[0].AsString()
		var cmdArgs []string
		for i := 1; i < len(args); i++ {
			if args[i].T != value.TypeString {
				return value.Void, fmt.Errorf("os.exec: arg %d must be string", i)
			}
			cmdArgs = append(cmdArgs, args[i].AsString())
		}
		cmd := exec.Command(cmdName, cmdArgs...)
		var stdout, stderr strings.Builder
		cmd.Stdout = &stdout
		cmd.Stderr = &stderr
		err := cmd.Run()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{
				value.NewString(stdout.String()),
				value.NewString(stderr.String()),
				value.NewErr(err.Error()),
			}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{
			value.NewString(stdout.String()),
			value.NewString(stderr.String()),
			value.Ok,
		}}
	}))
	return env
}
