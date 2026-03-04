package interp

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// fileErr translates Go os errors into a BASL error value with the appropriate kind.
func fileErrVal(err error, path string) value.Value {
	if errors.Is(err, os.ErrNotExist) {
		return value.NewErr(fmt.Sprintf("file not found: %s", path), value.ErrKindNotFound)
	}
	if errors.Is(err, os.ErrPermission) {
		return value.NewErr(fmt.Sprintf("permission denied: %s", path), value.ErrKindPermission)
	}
	if errors.Is(err, os.ErrExist) {
		return value.NewErr(fmt.Sprintf("file already exists: %s", path), value.ErrKindExists)
	}
	return value.NewErr(fmt.Sprintf("file error: %s", path), value.ErrKindIO)
}

func walkErrVal(err error, path string) value.Value {
	msg := strings.ToLower(err.Error())
	if strings.Contains(msg, "too many links") {
		return value.NewErr(fmt.Sprintf("symlink cycle detected while walking: %s", path), value.ErrKindState)
	}
	return fileErrVal(err, path)
}

func newFileEntry(path string, info os.FileInfo) value.Value {
	return value.Value{
		T: value.TypeObject,
		Data: &value.ObjectVal{
			ClassName: "file.Entry",
			Fields: map[string]value.Value{
				"path":     value.NewString(path),
				"name":     value.NewString(info.Name()),
				"is_dir":   value.NewBool(info.IsDir()),
				"size":     value.NewI32(int32(info.Size())),
				"mode":     value.NewI32(int32(info.Mode())),
				"mod_time": value.NewString(info.ModTime().Format("2006-01-02T15:04:05Z07:00")),
			},
		},
	}
}

func newWalkIssue(path string, errVal value.Value) value.Value {
	return value.Value{
		T: value.TypeObject,
		Data: &value.ObjectVal{
			ClassName: "file.WalkIssue",
			Fields: map[string]value.Value{
				"path": value.NewString(path),
				"err":  errVal,
			},
		},
	}
}

func walkDirKey(path string) (string, error) {
	abs, err := filepath.Abs(path)
	if err != nil {
		return "", err
	}
	resolved, err := filepath.EvalSymlinks(abs)
	if err != nil {
		return "", err
	}
	return filepath.Clean(resolved), nil
}

func walkEntries(root string, followLinks bool, bestEffort bool) ([]value.Value, []value.Value, value.Value) {
	entries := make([]value.Value, 0)
	issues := make([]value.Value, 0)

	recordIssue := func(path string, errVal value.Value) {
		issues = append(issues, newWalkIssue(path, errVal))
	}

	var walkErr value.Value = value.Ok

	var visit func(path string, active map[string]struct{}) bool
	visit = func(path string, active map[string]struct{}) bool {
		info, err := os.Lstat(path)
		if err != nil {
			errVal := walkErrVal(err, path)
			if bestEffort {
				recordIssue(path, errVal)
				return true
			}
			walkErr = errVal
			return false
		}

		entryInfo := info
		isDir := info.IsDir()
		if followLinks && (info.Mode()&os.ModeSymlink) != 0 {
			targetInfo, statErr := os.Stat(path)
			if statErr != nil {
				errVal := walkErrVal(statErr, path)
				if bestEffort {
					recordIssue(path, errVal)
					return true
				}
				walkErr = errVal
				return false
			}
			entryInfo = targetInfo
			isDir = targetInfo.IsDir()
		}

		entries = append(entries, newFileEntry(path, entryInfo))

		if !isDir {
			return true
		}

		if followLinks {
			key, keyErr := walkDirKey(path)
			if keyErr != nil {
				errVal := walkErrVal(keyErr, path)
				if bestEffort {
					recordIssue(path, errVal)
					return true
				}
				walkErr = errVal
				return false
			}
			if _, ok := active[key]; ok {
				errVal := value.NewErr(fmt.Sprintf("symlink cycle detected while walking: %s", path), value.ErrKindState)
				if bestEffort {
					recordIssue(path, errVal)
					return true
				}
				walkErr = errVal
				return false
			}
			active[key] = struct{}{}
			defer delete(active, key)
		}

		children, readErr := os.ReadDir(path)
		if readErr != nil {
			errVal := walkErrVal(readErr, path)
			if bestEffort {
				recordIssue(path, errVal)
				return true
			}
			walkErr = errVal
			return false
		}
		sort.Slice(children, func(i, j int) bool {
			return children[i].Name() < children[j].Name()
		})
		for _, child := range children {
			childPath := filepath.Join(path, child.Name())
			if !visit(childPath, active) {
				return false
			}
		}
		return true
	}

	if !visit(filepath.Clean(root), make(map[string]struct{})) {
		return nil, issues, walkErr
	}
	return entries, issues, value.Ok
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
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), fileErrVal(err, path)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(data)), value.Ok}}
	}))
	env.Define("write_all", value.NewNativeFunc("file.write_all", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.write_all: expected (string path, string data)")
		}
		path := args[0].AsString()
		if err := os.WriteFile(path, []byte(args[1].AsString()), 0644); err != nil {
			return fileErrVal(err, path), nil
		}
		return value.Ok, nil
	}))
	// Alias for write_all
	env.Define("write", value.NewNativeFunc("file.write", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.write: expected (string path, string data)")
		}
		path := args[0].AsString()
		if err := os.WriteFile(path, []byte(args[1].AsString()), 0644); err != nil {
			return fileErrVal(err, path), nil
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
			return fileErrVal(err, path), nil
		}
		defer f.Close()
		if _, err := f.WriteString(args[1].AsString()); err != nil {
			return fileErrVal(err, path), nil
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
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), fileErrVal(err, path)}}
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
			return fileErrVal(err, path), nil
		}
		return value.Ok, nil
	}))
	env.Define("rename", value.NewNativeFunc("file.rename", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.rename: expected (string old, string new)")
		}
		if err := os.Rename(args[0].AsString(), args[1].AsString()); err != nil {
			return fileErrVal(err, args[0].AsString()), nil
		}
		return value.Ok, nil
	}))
	env.Define("copy", value.NewNativeFunc("file.copy", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.copy: expected (string src, string dst)")
		}
		src := args[0].AsString()
		dst := args[1].AsString()

		srcFile, err := os.Open(src)
		if err != nil {
			return fileErrVal(err, src), nil
		}
		defer srcFile.Close()

		dstFile, err := os.Create(dst)
		if err != nil {
			return fileErrVal(err, dst), nil
		}
		defer dstFile.Close()

		if _, err := io.Copy(dstFile, srcFile); err != nil {
			return fileErrVal(err, dst), nil
		}

		return value.Ok, nil
	}))
	env.Define("symlink", value.NewNativeFunc("file.symlink", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.symlink: expected (string target, string link)")
		}
		target := args[0].AsString()
		link := args[1].AsString()
		if err := os.Symlink(target, link); err != nil {
			return fileErrVal(err, link), nil
		}
		return value.Ok, nil
	}))
	env.Define("link", value.NewNativeFunc("file.link", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("file.link: expected (string target, string link)")
		}
		target := args[0].AsString()
		link := args[1].AsString()
		if err := os.Link(target, link); err != nil {
			return fileErrVal(err, link), nil
		}
		return value.Ok, nil
	}))
	env.Define("readlink", value.NewNativeFunc("file.readlink", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.readlink: expected string path")
		}
		path := args[0].AsString()
		target, err := os.Readlink(path)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), fileErrVal(err, path)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(target), value.Ok}}
	}))
	env.Define("chmod", value.NewNativeFunc("file.chmod", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeI32 {
			return value.Void, fmt.Errorf("file.chmod: expected (string path, i32 mode)")
		}
		path := args[0].AsString()
		mode := os.FileMode(args[1].AsI32())
		if err := os.Chmod(path, mode); err != nil {
			return fileErrVal(err, path), nil
		}
		return value.Ok, nil
	}))
	env.Define("mkdir", value.NewNativeFunc("file.mkdir", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.mkdir: expected string path")
		}
		path := args[0].AsString()
		if err := os.MkdirAll(path, 0755); err != nil {
			return fileErrVal(err, path), nil
		}
		return value.Ok, nil
	}))
	env.Define("touch", value.NewNativeFunc("file.touch", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.touch: expected string path")
		}
		path := args[0].AsString()

		// Check if file exists
		_, err := os.Stat(path)
		if err == nil {
			// File exists, update timestamp
			now := time.Now()
			if err := os.Chtimes(path, now, now); err != nil {
				return fileErrVal(err, path), nil
			}
		} else if errors.Is(err, os.ErrNotExist) {
			// File doesn't exist, create it
			f, err := os.Create(path)
			if err != nil {
				return fileErrVal(err, path), nil
			}
			f.Close()
		} else {
			// Other error
			return fileErrVal(err, path), nil
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
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), fileErrVal(err, path)}}
		}
		elems := make([]value.Value, len(entries))
		for i, e := range entries {
			elems[i] = value.NewString(e.Name())
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(elems), value.Ok}}
	}))
	// Alias for consistency with read_all
	env.Define("read_dir", value.NewNativeFunc("file.read_dir", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.read_dir: expected string path")
		}
		path := args[0].AsString()
		entries, err := os.ReadDir(path)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), fileErrVal(err, path)}}
		}
		elems := make([]value.Value, len(entries))
		for i, e := range entries {
			elems[i] = value.NewString(e.Name())
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(elems), value.Ok}}
	}))
	env.Define("walk", value.NewNativeFunc("file.walk", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.walk: expected string root")
		}
		root := args[0].AsString()
		entries, _, walkErr := walkEntries(root, false, false)
		if !walkErr.IsOk() {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), walkErr}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(entries), value.Ok}}
	}))
	env.Define("walk_follow_links", value.NewNativeFunc("file.walk_follow_links", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.walk_follow_links: expected string root")
		}
		root := args[0].AsString()
		entries, _, walkErr := walkEntries(root, true, false)
		if !walkErr.IsOk() {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), walkErr}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(entries), value.Ok}}
	}))
	env.Define("walk_best_effort", value.NewNativeFunc("file.walk_best_effort", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.walk_best_effort: expected string root")
		}
		root := args[0].AsString()
		entries, issues, _ := walkEntries(root, false, true)
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(entries), value.NewArray(issues)}}
	}))
	env.Define("walk_follow_links_best_effort", value.NewNativeFunc("file.walk_follow_links_best_effort", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("file.walk_follow_links_best_effort: expected string root")
		}
		root := args[0].AsString()
		entries, issues, _ := walkEntries(root, true, true)
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(entries), value.NewArray(issues)}}
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
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("file.open: invalid mode: "+mode, value.ErrKindArg)}}
		}
		f, err := os.OpenFile(path, flag, 0644)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, fileErrVal(err, path)}}
		}
		obj := &value.ObjectVal{
			ClassName: "file.File",
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
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, fileErrVal(err, args[0].AsString())}}
		}
		obj := &value.ObjectVal{
			ClassName: "file.FileStat",
			Fields: map[string]value.Value{
				"size":     value.NewI32(int32(info.Size())),
				"is_dir":   value.NewBool(info.IsDir()),
				"mod_time": value.NewString(info.ModTime().Format("2006-01-02T15:04:05Z07:00")),
				"name":     value.NewString(info.Name()),
				"mode":     value.NewI32(int32(info.Mode())),
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
				return value.NewErr(err.Error(), value.ErrKindIO), nil
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
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
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
						return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("EOF", value.ErrKindEOF)}}
					}
					return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
				}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(line)), value.Ok}}
		}), nil
	case "close":
		return value.NewNativeFunc("File.close", func(args []value.Value) (value.Value, error) {
			if err := f.Close(); err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: File has no method '%s'", line, method)
	}
}
