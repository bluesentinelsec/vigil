package interp

import (
	"archive/tar"
	"archive/zip"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ── archive module ──

func (interp *Interpreter) makeArchiveModule() *Env {
	env := NewEnv(nil)

	env.Define("tar_create", value.NewNativeFunc("archive.tar_create", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeArray {
			return value.Void, fmt.Errorf("archive.tar_create: expected (string path, array<string> files)")
		}
		outPath := args[0].AsString()
		files := args[1].AsArray()
		out, err := os.Create(outPath)
		if err != nil {
			return value.NewErr(err.Error(), value.ErrKindIO), nil
		}
		defer out.Close()
		tw := tar.NewWriter(out)
		defer tw.Close()
		for _, fv := range files.Elems {
			p := fv.AsString()
			info, err := os.Stat(p)
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			hdr, err := tar.FileInfoHeader(info, "")
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			hdr.Name = filepath.Base(p)
			if err := tw.WriteHeader(hdr); err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			if !info.IsDir() {
				data, err := os.ReadFile(p)
				if err != nil {
					return value.NewErr(err.Error(), value.ErrKindIO), nil
				}
				if _, err := tw.Write(data); err != nil {
					return value.NewErr(err.Error(), value.ErrKindIO), nil
				}
			}
		}
		return value.Ok, nil
	}))

	env.Define("tar_extract", value.NewNativeFunc("archive.tar_extract", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("archive.tar_extract: expected (string path, string dest)")
		}
		f, err := os.Open(args[0].AsString())
		if err != nil {
			return value.NewErr(err.Error(), value.ErrKindIO), nil
		}
		defer f.Close()
		dest := args[1].AsString()
		tr := tar.NewReader(f)
		for {
			hdr, err := tr.Next()
			if err == io.EOF {
				break
			}
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			target := filepath.Join(dest, hdr.Name)
			if hdr.Typeflag == tar.TypeDir {
				os.MkdirAll(target, 0755)
				continue
			}
			os.MkdirAll(filepath.Dir(target), 0755)
			data, err := io.ReadAll(tr)
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			if err := os.WriteFile(target, data, os.FileMode(hdr.Mode)); err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
		}
		return value.Ok, nil
	}))

	env.Define("zip_create", value.NewNativeFunc("archive.zip_create", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeArray {
			return value.Void, fmt.Errorf("archive.zip_create: expected (string path, array<string> files)")
		}
		outPath := args[0].AsString()
		files := args[1].AsArray()
		out, err := os.Create(outPath)
		if err != nil {
			return value.NewErr(err.Error(), value.ErrKindIO), nil
		}
		defer out.Close()
		zw := zip.NewWriter(out)
		defer zw.Close()
		for _, fv := range files.Elems {
			p := fv.AsString()
			data, err := os.ReadFile(p)
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			w, err := zw.Create(filepath.Base(p))
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			if _, err := w.Write(data); err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
		}
		return value.Ok, nil
	}))

	env.Define("zip_extract", value.NewNativeFunc("archive.zip_extract", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("archive.zip_extract: expected (string path, string dest)")
		}
		r, err := zip.OpenReader(args[0].AsString())
		if err != nil {
			return value.NewErr(err.Error(), value.ErrKindIO), nil
		}
		defer r.Close()
		dest := args[1].AsString()
		for _, f := range r.File {
			target := filepath.Join(dest, f.Name)
			if f.FileInfo().IsDir() {
				os.MkdirAll(target, 0755)
				continue
			}
			os.MkdirAll(filepath.Dir(target), 0755)
			rc, err := f.Open()
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			data, err := io.ReadAll(rc)
			rc.Close()
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			if err := os.WriteFile(target, data, f.Mode()); err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
		}
		return value.Ok, nil
	}))

	return env
}
