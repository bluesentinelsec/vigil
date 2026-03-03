// Package basl provides the BASL scripting language runtime.
package basl

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/bluesentinelsec/basl/pkg/basl/ffi"
	"github.com/bluesentinelsec/basl/pkg/basl/interp"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// Version is the BASL version string.
const Version = "0.1.2"

// VM is the BASL virtual machine (AST interpreter).
type VM struct {
	interp *interp.Interpreter
}

// Config holds VM configuration options.
type Config struct {
	Args        []string // script arguments available via os.args()
	SearchPaths []string // additional module search directories
}

// NewVM creates a new BASL VM with the given configuration.
func NewVM(cfg Config) *VM {
	vm := &VM{interp: interp.New()}
	vm.interp.RegisterScriptArgs(cfg.Args)
	for _, p := range cfg.SearchPaths {
		vm.interp.AddSearchPath(p)
	}
	return vm
}

// EvalString evaluates BASL source code from a string. Returns exit code.
func (vm *VM) EvalString(src string) (int, error) {
	return vm.eval(src)
}

// EvalBytes evaluates BASL source code from bytes. Returns exit code.
func (vm *VM) EvalBytes(src []byte) (int, error) {
	return vm.eval(string(src))
}

// EvalFile evaluates a BASL script file. Returns exit code.
func (vm *VM) EvalFile(path string) (int, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return 1, fmt.Errorf("failed to read %s: %w", path, err)
	}
	// Add script directory as search path
	dir, _ := filepath.Abs(filepath.Dir(path))
	vm.interp.AddSearchPath(dir)
	return vm.eval(string(data))
}

// RegisterFunc registers a native Go function callable from BASL.
func (vm *VM) RegisterFunc(name string, fn func(args []value.Value) (value.Value, error)) {
	vm.interp.RegisterNativeFunc(name, fn)
}

// RegisterModule registers a native Go module accessible via import.
func (vm *VM) RegisterModule(name string, funcs map[string]func([]value.Value) (value.Value, error)) {
	vm.interp.RegisterNativeModule(name, funcs)
}

// RegisterEmbeddedFS registers an embedded filesystem for module resolution.
func (vm *VM) RegisterEmbeddedFS(prefix string, fsys fs.FS) {
	vm.interp.RegisterEmbeddedFS(prefix, fsys)
}

// SetFFIPolicy configures the FFI security policy.
func (vm *VM) SetFFIPolicy(p ffi.Policy) {
	vm.interp.SetFFIPolicy(&p)
}

func (vm *VM) eval(src string) (int, error) {
	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		return 1, fmt.Errorf("lexer error: %w", err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		return 1, fmt.Errorf("parse error: %w", err)
	}
	return vm.interp.Exec(prog)
}
