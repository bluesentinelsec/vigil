package interp

import (
	"fmt"
	"io/fs"
	"sort"
	"sync"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/ffi"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// signalReturn is used to unwind the call stack on return.
type signalReturn struct {
	Values []value.Value
}

func (s *signalReturn) Error() string { return "return signal" }

// signalBreak / signalContinue for loop control.
type signalBreak struct{}
type signalContinue struct{}

func (s *signalBreak) Error() string    { return "break signal" }
func (s *signalContinue) Error() string { return "continue signal" }

// Env is a scope chain.
type deferredCall struct {
	callee value.Value
	args   []value.Value
}

type Env struct {
	vars   map[string]value.Value
	types  map[string]*ast.TypeExpr
	consts map[string]bool
	// returnType is set on the root environment for a function invocation.
	// Nested block scopes walk parent links to find it.
	returnType *ast.ReturnType
	parent     *Env
	defers     *[]deferredCall // shared within a function call
}

func NewEnv(parent *Env) *Env {
	e := &Env{vars: make(map[string]value.Value), parent: parent}
	if parent != nil {
		e.defers = parent.defers
	}
	return e
}

func (e *Env) Get(name string) (value.Value, bool) {
	if v, ok := e.vars[name]; ok {
		return v, true
	}
	if e.parent != nil {
		return e.parent.Get(name)
	}
	return value.Void, false
}

func (e *Env) Set(name string, v value.Value) bool {
	if _, ok := e.vars[name]; ok {
		if e.consts != nil && e.consts[name] {
			return false // const — cannot reassign
		}
		e.vars[name] = v
		return true
	}
	if e.parent != nil {
		return e.parent.Set(name, v)
	}
	return false
}

func (e *Env) Define(name string, v value.Value) {
	e.vars[name] = v
}

func (e *Env) DefineTyped(name string, v value.Value, t *ast.TypeExpr) {
	e.vars[name] = v
	if t != nil {
		if e.types == nil {
			e.types = make(map[string]*ast.TypeExpr)
		}
		e.types[name] = t
	}
}

func (e *Env) GetType(name string) *ast.TypeExpr {
	if e.types != nil {
		if t, ok := e.types[name]; ok {
			return t
		}
	}
	if e.parent != nil {
		return e.parent.GetType(name)
	}
	return nil
}

func (e *Env) MarkConst(name string) {
	if e.consts == nil {
		e.consts = make(map[string]bool)
	}
	e.consts[name] = true
}

func (e *Env) IsConst(name string) bool {
	if e.consts != nil && e.consts[name] {
		return true
	}
	if e.parent != nil {
		return e.parent.IsConst(name)
	}
	return false
}

func (e *Env) CurrentReturnType() *ast.ReturnType {
	if e.returnType != nil {
		return e.returnType
	}
	if e.parent != nil {
		return e.parent.CurrentReturnType()
	}
	return nil
}

type Interpreter struct {
	globals        *Env
	modules        map[string]*Env
	builtinModules map[string]*Env
	interfaces     map[string]*value.InterfaceVal // registered interfaces
	loader         *ModuleLoader
	PrintFn        func(string)
	LogFn          func(level, msg string) // host-side log handler; nil = default stderr
	scriptArgs     []string
	ffiPolicy      *ffi.Policy
	baslLogHandler *value.Value // BASL-side log.set_handler(fn)
	gil            sync.Mutex   // Global Interpreter Lock — serializes all BASL execution
	debugger       *Debugger    // nil unless running under basl debug
}

func New() *Interpreter {
	interp := &Interpreter{
		globals:        NewEnv(nil),
		modules:        make(map[string]*Env),
		builtinModules: make(map[string]*Env),
		interfaces:     make(map[string]*value.InterfaceVal),
		PrintFn:        func(s string) { fmt.Print(s) },
		ffiPolicy:      &ffi.Policy{Enabled: true},
	}
	interp.loader = newModuleLoader(interp)
	interp.registerBuiltins()
	interp.registerStdlib()
	return interp
}

func (interp *Interpreter) registerBuiltins() {
	// Language-level primitives only — no library functions as bare globals
	interp.globals.Define("ok", value.Ok)
	interp.globals.Define("string_from_bytes", value.NewNativeFunc("string_from_bytes", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeArray {
			return value.Void, fmt.Errorf("string_from_bytes: expected array<u8>")
		}
		arr := args[0].AsArray()
		b := make([]byte, len(arr.Elems))
		for i, e := range arr.Elems {
			if e.T != value.TypeU8 {
				return value.Void, fmt.Errorf("string_from_bytes: element %d is not u8", i)
			}
			b[i] = e.AsU8()
		}
		return value.NewString(string(b)), nil
	}))
}

// RegisterScriptArgs makes script arguments available via os.args() pattern.
func (interp *Interpreter) RegisterScriptArgs(args []string) {
	interp.scriptArgs = args
}

// GetScriptArgs returns the registered script arguments as a BASL array.
func (interp *Interpreter) GetScriptArgs() value.Value {
	var elems []value.Value
	for _, a := range interp.scriptArgs {
		elems = append(elems, value.NewString(a))
	}
	return value.NewArray(elems)
}

// RegisterNativeFunc registers a Go function callable from BASL.
func (interp *Interpreter) RegisterNativeFunc(name string, fn func([]value.Value) (value.Value, error)) {
	interp.globals.Define(name, value.NewNativeFunc(name, fn))
}

// AddSearchPath adds a directory to the module search path.
func (interp *Interpreter) AddSearchPath(dir string) {
	interp.loader.SearchPaths = append(interp.loader.SearchPaths, dir)
}

// RegisterEmbeddedFS registers an embedded filesystem for module resolution.
// Import paths starting with prefix will be resolved from this FS.
func (interp *Interpreter) RegisterEmbeddedFS(prefix string, fsys fs.FS) {
	interp.loader.EmbeddedFS[prefix] = fsys
}

// RegisterNativeModule registers a Go-implemented module accessible via import.
func (interp *Interpreter) RegisterNativeModule(name string, funcs map[string]func([]value.Value) (value.Value, error)) {
	env := NewEnv(nil)
	for fname, fn := range funcs {
		env.Define(fname, value.NewNativeFunc(name+"."+fname, fn))
	}
	interp.builtinModules[name] = env
}

// BuiltinModuleNames returns the names of builtin native modules available via import.
func BuiltinModuleNames() []string {
	interp := New()
	names := make([]string, 0, len(interp.builtinModules))
	for name := range interp.builtinModules {
		names = append(names, name)
	}
	sort.Strings(names)
	return names
}

// SetFFIPolicy configures the FFI security policy.
func (interp *Interpreter) SetFFIPolicy(p *ffi.Policy) {
	interp.ffiPolicy = p
}

// SetDebugger attaches a debugger to the interpreter.
func (interp *Interpreter) SetDebugger(d *Debugger) {
	interp.debugger = d
}

// Exec runs a program. Returns the exit code from main().
func (interp *Interpreter) Exec(prog *ast.Program) (int, error) {
	interp.gil.Lock()
	defer interp.gil.Unlock()

	// First pass: register top-level declarations
	for _, d := range prog.Decls {
		if err := interp.execTopDecl(d); err != nil {
			return 1, err
		}
	}
	// Find and call main
	mainVal, ok := interp.globals.Get("main")
	if !ok {
		return 1, fmt.Errorf("no main function defined — every BASL program needs: fn main() -> i32 { return 0; }")
	}
	result, err := interp.callFunc(mainVal, nil)
	if err != nil {
		return 1, err
	}
	// main returns i32 or err
	switch result.T {
	case value.TypeI32:
		return int(result.AsI32()), nil
	case value.TypeErr:
		if result.IsOk() {
			return 0, nil
		}
		return 1, fmt.Errorf("%s", result.AsErr().Message)
	default:
		return 0, nil
	}
}

// EvalString lexes, parses, and executes a BASL program from a string.
func (interp *Interpreter) EvalString(src string) (int, error) {
	tokens, err := lexer.New(src).Tokenize()
	if err != nil {
		return 1, fmt.Errorf("lexer: %w", err)
	}
	prog, err := parser.New(tokens).Parse()
	if err != nil {
		return 1, fmt.Errorf("parser: %w", err)
	}
	return interp.Exec(prog)
}

// ReplEval evaluates a single REPL line. Returns a displayable result string.
func (interp *Interpreter) ReplEval(src string) (string, error) {
	interp.gil.Lock()
	defer interp.gil.Unlock()

	tokens, err := lexer.New(src).Tokenize()
	if err != nil {
		return "", err
	}
	p := parser.New(tokens)
	decl, stmt, err := p.ParseReplLine()
	if err != nil {
		return "", err
	}
	if decl != nil {
		return "", interp.execTopDecl(decl)
	}
	// For expression statements, capture and return the value
	if es, ok := stmt.(*ast.ExprStmt); ok {
		val, err := interp.evalExpr(es.Expr, interp.globals)
		if err != nil {
			return "", err
		}
		if val.T == value.TypeVoid {
			return "", nil
		}
		if val.T == value.TypeErr && val.IsOk() {
			return "", nil
		}
		return val.String(), nil
	}
	return "", interp.execStmt(stmt, interp.globals)
}

// EvalBytes lexes, parses, and executes a BASL program from bytes.
func (interp *Interpreter) EvalBytes(src []byte) (int, error) {
	return interp.EvalString(string(src))
}

func (interp *Interpreter) execTopDecl(d ast.Decl) error {
	switch d := d.(type) {
	case *ast.FnDecl:
		fv := &value.FuncVal{
			Name:   d.Name,
			Body:   d.Body,
			Return: d.Return,
		}
		for _, p := range d.Params {
			fv.Params = append(fv.Params, value.FuncParam{Type: p.Type.Name, TypeExpr: p.Type, Name: p.Name})
		}
		interp.globals.Define(d.Name, value.NewFunc(fv))
	case *ast.VarDecl:
		val, err := interp.evalExpr(d.Init, interp.globals)
		if err != nil {
			return err
		}
		if err := interp.checkType(d.Type, val, d.Line); err != nil {
			return err
		}
		interp.globals.DefineTyped(d.Name, val, d.Type)
	case *ast.ImportDecl:
		modName := d.Path
		alias := d.Alias
		if alias == "" {
			parts := splitPath(modName)
			alias = parts[len(parts)-1]
		}
		modEnv, err := interp.loader.Resolve(modName)
		if err != nil {
			return fmt.Errorf("line %d: %s", d.Line, err)
		}
		interp.globals.Define(alias, value.Value{T: value.TypeModule, Data: modEnv})
	case *ast.ClassDecl:
		cv := &value.ClassVal{
			Name:       d.Name,
			Implements: d.Implements,
			Methods:    make(map[string]*value.FuncVal),
		}
		for _, f := range d.Fields {
			cv.Fields = append(cv.Fields, value.ClassFieldDef{
				Name: f.Name, Type: f.Type.Name, Pub: f.Pub,
			})
		}
		for _, m := range d.Methods {
			fv := &value.FuncVal{Name: m.Name, Body: m.Body, Return: m.Return}
			for _, p := range m.Params {
				fv.Params = append(fv.Params, value.FuncParam{Type: p.Type.Name, TypeExpr: p.Type, Name: p.Name})
			}
			if m.Name == "init" {
				cv.Init = fv
			} else {
				cv.Methods[m.Name] = fv
			}
		}
		// Validate implements
		for _, ifaceName := range d.Implements {
			iface, ok := interp.interfaces[ifaceName]
			if !ok {
				return fmt.Errorf("line %d: class %s implements unknown interface %q", d.Line, d.Name, ifaceName)
			}
			for _, req := range iface.Methods {
				m, ok := cv.Methods[req.Name]
				if !ok {
					return fmt.Errorf("line %d: class %s missing method %q required by interface %s", d.Line, d.Name, req.Name, ifaceName)
				}
				// Check param count
				if len(m.Params) != len(req.ParamTypes) {
					return fmt.Errorf("line %d: class %s method %s has %d params, interface %s requires %d", d.Line, d.Name, req.Name, len(m.Params), ifaceName, len(req.ParamTypes))
				}
			}
		}
		interp.globals.Define(d.Name, value.NewClass(cv))
	case *ast.ConstDecl:
		val, err := interp.evalExpr(d.Init, interp.globals)
		if err != nil {
			return err
		}
		interp.globals.Define(d.Name, val)
		interp.globals.MarkConst(d.Name)
	case *ast.EnumDecl:
		// Register each variant as EnumName.Variant = i32
		enumEnv := NewEnv(nil)
		var nextVal int32
		for _, v := range d.Variants {
			if v.Value != nil {
				ev, err := interp.evalExpr(v.Value, interp.globals)
				if err != nil {
					return err
				}
				nextVal = ev.AsI32()
			}
			enumEnv.Define(v.Name, value.NewI32(nextVal))
			nextVal++
		}
		interp.globals.Define(d.Name, value.Value{T: value.TypeModule, Data: enumEnv})
	case *ast.InterfaceDecl:
		iv := &value.InterfaceVal{Name: d.Name}
		for _, m := range d.Methods {
			sig := value.InterfaceMethodSig{Name: m.Name}
			for _, p := range m.Params {
				sig.ParamTypes = append(sig.ParamTypes, p.Type.Name)
			}
			if m.Return != nil {
				for _, rt := range m.Return.Types {
					sig.ReturnTypes = append(sig.ReturnTypes, rt.Name)
				}
			}
			iv.Methods = append(iv.Methods, sig)
		}
		interp.interfaces[d.Name] = iv
		interp.globals.Define(d.Name, value.Value{T: value.TypeModule, Data: NewEnv(nil)})
	}
	return nil
}

func splitPath(s string) []string {
	var parts []string
	current := ""
	for _, c := range s {
		if c == '/' {
			if current != "" {
				parts = append(parts, current)
			}
			current = ""
		} else {
			current += string(c)
		}
	}
	if current != "" {
		parts = append(parts, current)
	}
	if len(parts) == 0 {
		return []string{s}
	}
	return parts
}
