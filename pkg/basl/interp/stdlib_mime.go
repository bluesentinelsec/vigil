package interp

import (
	"fmt"
	"mime"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ── mime module ──

func (interp *Interpreter) makeMimeModule() *Env {
	env := NewEnv(nil)

	env.Define("type_by_ext", value.NewNativeFunc("mime.type_by_ext", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("mime.type_by_ext: expected string ext")
		}
		ext := args[0].AsString()
		if !strings.HasPrefix(ext, ".") {
			ext = "." + ext
		}
		t := mime.TypeByExtension(ext)
		return value.NewString(t), nil
	}))

	env.Define("ext_by_type", value.NewNativeFunc("mime.ext_by_type", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("mime.ext_by_type: expected string mime_type")
		}
		exts, err := mime.ExtensionsByType(args[0].AsString())
		if err != nil || len(exts) == 0 {
			return value.NewString(""), nil
		}
		return value.NewString(exts[0]), nil
	}))

	return env
}
