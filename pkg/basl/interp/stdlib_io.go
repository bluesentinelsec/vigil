package interp

import (
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeIoModule() *Env {
	env := NewEnv(nil)
	env.Define("read_line", value.NewNativeFunc("io.read_line", func(args []value.Value) (value.Value, error) {
		var line []byte
		buf := make([]byte, 1)
		for {
			n, err := os.Stdin.Read(buf)
			if n > 0 {
				if buf[0] == '\n' {
					break
				}
				line = append(line, buf[0])
			}
			if err != nil {
				if err == io.EOF {
					if len(line) > 0 {
						break
					}
					return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("EOF")}}
				}
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
			}
		}
		s := strings.TrimRight(string(line), "\r")
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(s), value.Ok}}
	}))
	env.Define("input", value.NewNativeFunc("io.input", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("io.input: expected (string prompt)")
		}
		fmt.Fprint(os.Stdout, args[0].AsString())
		var line []byte
		buf := make([]byte, 1)
		for {
			n, err := os.Stdin.Read(buf)
			if n > 0 {
				if buf[0] == '\n' {
					break
				}
				line = append(line, buf[0])
			}
			if err != nil {
				if err == io.EOF {
					if len(line) > 0 {
						break
					}
					return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("EOF")}}
				}
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
			}
		}
		s := strings.TrimRight(string(line), "\r")
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(s), value.Ok}}
	}))

	// helper: prompt + read line
	readLine := func(prompt string) (string, error) {
		fmt.Fprint(os.Stdout, prompt)
		var line []byte
		buf := make([]byte, 1)
		for {
			n, err := os.Stdin.Read(buf)
			if n > 0 {
				if buf[0] == '\n' {
					break
				}
				line = append(line, buf[0])
			}
			if err != nil {
				if err == io.EOF && len(line) > 0 {
					break
				}
				return "", err
			}
		}
		return strings.TrimRight(string(line), "\r"), nil
	}

	env.Define("read_f64", value.NewNativeFunc("io.read_f64", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("io.read_f64: expected (string prompt)")
		}
		s, err := readLine(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(0), value.NewErr(err.Error())}}
		}
		v, err := strconv.ParseFloat(s, 64)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(0), value.NewErr("invalid number")}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(v), value.Ok}}
	}))

	env.Define("read_i32", value.NewNativeFunc("io.read_i32", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("io.read_i32: expected (string prompt)")
		}
		s, err := readLine(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(0), value.NewErr(err.Error())}}
		}
		v, err := strconv.Atoi(s)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(0), value.NewErr("invalid integer")}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(int32(v)), value.Ok}}
	}))

	env.Define("read_string", value.NewNativeFunc("io.read_string", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("io.read_string: expected (string prompt)")
		}
		s, err := readLine(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(s), value.Ok}}
	}))

	env.Define("read_all", value.NewNativeFunc("io.read_all", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("io.read_all: expected 0 arguments, got %d", len(args))
		}
		// Read all of stdin
		data, err := io.ReadAll(os.Stdin)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(data)), value.Ok}}
	}))

	return env
}
