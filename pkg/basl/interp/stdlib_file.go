package interp

import (
	"errors"
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// fileErr translates Go os errors into user-friendly BASL error messages.
func fileErr(err error, path string) string {
	if errors.Is(err, os.ErrNotExist) {
		return fmt.Sprintf("file not found: %s", path)
	}
	if errors.Is(err, os.ErrPermission) {
		return fmt.Sprintf("permission denied: %s", path)
	}
	if errors.Is(err, os.ErrExist) {
		return fmt.Sprintf("file already exists: %s", path)
	}
	return fmt.Sprintf("file error: %s", path)
}

func (interp *Interpreter) makeFileModule() *Env {
	env := NewEnv(nil)
	env.Define("read_all", value.NewNativeFunc("file.read_all", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.read_all: expected string path")
		}
		path := args[0].AsString()
		data, err := os.ReadFile(path)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(fileErr(err, path))}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(data)), value.Ok}}
	}))
	env.Define("write_all", value.NewNativeFunc("file.write_all", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.write_all: expected (string path, string data)")
		}
		path := args[0].AsString()
		if err := os.WriteFile(path, []byte(args[1].AsString()), 0644); err != nil {
			return value.NewErr(fileErr(err, path)), nil
		}
		return value.Ok, nil
	}))
	env.Define("append", value.NewNativeFunc("file.append", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.append: expected (string path, string data)")
		}
		path := args[0].AsString()
		f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			return value.NewErr(fileErr(err, path)), nil
		}
		defer f.Close()
		if _, err := f.WriteString(args[1].AsString()); err != nil {
			return value.NewErr(fileErr(err, path)), nil
		}
		return value.Ok, nil
	}))
	env.Define("read_lines", value.NewNativeFunc("file.read_lines", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.read_lines: expected string path")
		}
		path := args[0].AsString()
		data, err := os.ReadFile(path)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), value.NewErr(fileErr(err, path))}}
		}
		lines := strings.Split(strings.TrimRight(string(data), "\n"), "\n")
		elems := make([]value.Value, len(lines))
		for i, l := range lines {
			elems[i] = value.NewString(l)
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(elems), value.Ok}}
	}))
	env.Define("remove", value.NewNativeFunc("file.remove", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.remove: expected string path")
		}
		path := args[0].AsString()
		if err := os.Remove(path); err != nil {
			return value.NewErr(fileErr(err, path)), nil
		}
		return value.Ok, nil
	}))
	env.Define("rename", value.NewNativeFunc("file.rename", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.rename: expected (string old, string new)")
		}
		if err := os.Rename(args[0].AsString(), args[1].AsString()); err != nil {
			return value.NewErr(fileErr(err, args[0].AsString())), nil
		}
		return value.Ok, nil
	}))
	env.Define("mkdir", value.NewNativeFunc("file.mkdir", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.mkdir: expected string path")
		}
		path := args[0].AsString()
		if err := os.MkdirAll(path, 0755); err != nil {
			return value.NewErr(fileErr(err, path)), nil
		}
		return value.Ok, nil
	}))
	env.Define("list_dir", value.NewNativeFunc("file.list_dir", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.list_dir: expected string path")
		}
		path := args[0].AsString()
		entries, err := os.ReadDir(path)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), value.NewErr(fileErr(err, path))}}
		}
		elems := make([]value.Value, len(entries))
		for i, e := range entries {
			elems[i] = value.NewString(e.Name())
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(elems), value.Ok}}
	}))
	env.Define("exists", value.NewNativeFunc("file.exists", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.exists: expected string path")
		}
		_, err := os.Stat(args[0].AsString())
		return value.NewBool(err == nil), nil
	}))

	// Phase 3: open, stat, File handle methods
	env.Define("open", value.NewNativeFunc("file.open", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.open: expected (string path, string mode)")
		}
		path := args[0].AsString()
		mode := args[1].AsString()
		var flag int
		switch mode {
		case "r":
			flag = os.O_RDONLY
		case "w":
			flag = os.O_WRONLY | os.O_CREATE | os.O_TRUNC
		case "a":
			flag = os.O_WRONLY | os.O_CREATE | os.O_APPEND
		case "rw":
			flag = os.O_RDWR | os.O_CREATE
		default:
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("file.open: invalid mode: " + mode)}}
		}
		f, err := os.OpenFile(path, flag, 0644)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}
		obj := &value.ObjectVal{
			ClassName: "File",
			Fields:    map[string]value.Value{"__file": {T: value.TypeI32, Data: nil}},
		}
		obj.Fields["__file"] = value.Value{T: value.TypeString, Data: f}
		return value.Void, &MultiReturnVal{Values: []value.Value{
			{T: value.TypeObject, Data: obj},
			value.Ok,
		}}
	}))

	env.Define("stat", value.NewNativeFunc("file.stat", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.stat: expected string path")
		}
		info, err := os.Stat(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}
		obj := &value.ObjectVal{
			ClassName: "FileStat",
			Fields: map[string]value.Value{
				"size":     value.NewI32(int32(info.Size())),
				"is_dir":   value.NewBool(info.IsDir()),
				"mod_time": value.NewString(info.ModTime().Format("2006-01-02T15:04:05Z07:00")),
				"name":     value.NewString(info.Name()),
			},
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{
			{T: value.TypeObject, Data: obj},
			value.Ok,
		}}
	}))

	return env
}

func (interp *Interpreter) fileMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	f, ok := o.Fields["__file"].Data.(*os.File)
	if !ok {
		return value.Void, fmt.Errorf("line %d: File handle is invalid", line)
	}
	switch method {
	case "write":
		return value.NewNativeFunc("File.write", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("File.write: expected string")
			}
			_, err := f.WriteString(args[0].AsString())
			if err != nil {
				return value.NewErr(err.Error()), nil
			}
			return value.Ok, nil
		}), nil
	case "read":
		return value.NewNativeFunc("File.read", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("File.read: expected i32 count")
			}
			buf := make([]byte, args[0].AsI32())
			n, err := f.Read(buf)
			if err != nil && err != io.EOF {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(buf[:n])), value.Ok}}
		}), nil
	case "read_line":
		return value.NewNativeFunc("File.read_line", func(args []value.Value) (value.Value, error) {
			var line []byte
			buf := make([]byte, 1)
			for {
				n, err := f.Read(buf)
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
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(line)), value.Ok}}
		}), nil
	case "close":
		return value.NewNativeFunc("File.close", func(args []value.Value) (value.Value, error) {
			if err := f.Close(); err != nil {
				return value.NewErr(err.Error()), nil
			}
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: File has no method '%s'", line, method)
	}
}
