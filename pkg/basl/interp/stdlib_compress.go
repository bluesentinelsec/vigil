package interp

import (
	"bytes"
	"compress/gzip"
	"compress/zlib"
	"fmt"
	"io"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ── compress module ──

func (interp *Interpreter) makeCompressModule() *Env {
	env := NewEnv(nil)

	env.Define("gzip", value.NewNativeFunc("compress.gzip", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("compress.gzip: expected string")
		}
		var buf bytes.Buffer
		w := gzip.NewWriter(&buf)
		if _, err := w.Write([]byte(args[0].AsString())); err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		if err := w.Close(); err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(buf.String()), value.Ok}}
	}))

	env.Define("gunzip", value.NewNativeFunc("compress.gunzip", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("compress.gunzip: expected string")
		}
		r, err := gzip.NewReader(bytes.NewReader([]byte(args[0].AsString())))
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindParse)}}
		}
		defer r.Close()
		data, err := io.ReadAll(r)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(data)), value.Ok}}
	}))

	env.Define("zlib", value.NewNativeFunc("compress.zlib", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("compress.zlib: expected string")
		}
		var buf bytes.Buffer
		w := zlib.NewWriter(&buf)
		if _, err := w.Write([]byte(args[0].AsString())); err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		if err := w.Close(); err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(buf.String()), value.Ok}}
	}))

	env.Define("unzlib", value.NewNativeFunc("compress.unzlib", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("compress.unzlib: expected string")
		}
		r, err := zlib.NewReader(bytes.NewReader([]byte(args[0].AsString())))
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindParse)}}
		}
		defer r.Close()
		data, err := io.ReadAll(r)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(data)), value.Ok}}
	}))

	return env
}
