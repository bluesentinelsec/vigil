package interp

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ModuleLoader resolves and loads BASL modules by name.
type ModuleLoader struct {
	// Search paths for filesystem modules (in priority order)
	SearchPaths []string
	// Embedded filesystems registered by host program
	EmbeddedFS map[string]fs.FS
	// Cache of already-loaded modules
	cache map[string]*Env
	// Interpreter reference for evaluating modules
	interp *Interpreter
}

func newModuleLoader(interp *Interpreter) *ModuleLoader {
	return &ModuleLoader{
		EmbeddedFS: make(map[string]fs.FS),
		cache:      make(map[string]*Env),
		interp:     interp,
	}
}

// Resolve attempts to find and load a module by name.
// Returns the module's exported environment.
func (ml *ModuleLoader) Resolve(name string) (*Env, error) {
	// Check cache first
	if env, ok := ml.cache[name]; ok {
		return env, nil
	}

	// 1. Check builtin modules
	if env, ok := ml.interp.builtinModules[name]; ok {
		ml.cache[name] = env
		return env, nil
	}

	// 2. Check embedded filesystems
	for prefix, efs := range ml.EmbeddedFS {
		relPath := name
		if len(prefix) > 0 {
			// If the import starts with the prefix, strip it
			if len(name) > len(prefix) && name[:len(prefix)] == prefix && name[len(prefix)] == '/' {
				relPath = name[len(prefix)+1:]
			} else if name == prefix {
				relPath = ""
			} else {
				continue
			}
		}
		filePath := relPath + ".basl"
		data, err := fs.ReadFile(efs, filePath)
		if err == nil {
			env, err := ml.loadSource(name, string(data))
			if err != nil {
				return nil, fmt.Errorf("module %q: %w", name, err)
			}
			return env, nil
		}
	}

	// 3. Search filesystem paths (cache by absolute path to prevent double-load)
	fileName := name + ".basl"
	for _, dir := range ml.SearchPaths {
		// Direct: <dir>/<name>.basl
		fullPath := filepath.Join(dir, fileName)
		absPath, err := filepath.Abs(fullPath)
		if err != nil {
			continue
		}
		if env, ok := ml.cache[absPath]; ok {
			ml.cache[name] = env
			return env, nil
		}
		data, err := os.ReadFile(absPath)
		if err == nil {
			env, err := ml.loadSource(name, string(data))
			if err != nil {
				return nil, fmt.Errorf("module %q (%s): %w", name, absPath, err)
			}
			ml.cache[absPath] = env
			return env, nil
		}

		// Package: <dir>/<name>/lib/<name>.basl (for deps/)
		pkgPath := filepath.Join(dir, name, "lib", fileName)
		absPkg, err := filepath.Abs(pkgPath)
		if err != nil {
			continue
		}
		if env, ok := ml.cache[absPkg]; ok {
			ml.cache[name] = env
			return env, nil
		}
		data, err = os.ReadFile(absPkg)
		if err == nil {
			env, err := ml.loadSource(name, string(data))
			if err != nil {
				return nil, fmt.Errorf("module %q (%s): %w", name, absPkg, err)
			}
			ml.cache[absPkg] = env
			return env, nil
		}
	}

	return nil, fmt.Errorf("module %q not found — check the import path or ensure the .basl file exists", name)
}

// loadSource parses and evaluates a module, returning only its pub exports.
func (ml *ModuleLoader) loadSource(name string, src string) (*Env, error) {
	if _, ok := ml.cache[name]; ok {
		return nil, fmt.Errorf("fatal: module %q loaded twice — this is a bug in the module loader", name)
	}
	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		return nil, err
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		return nil, err
	}

	// Create a module-level environment with access to builtins
	modEnv := NewEnv(ml.interp.globals)

	// Process the module's declarations
	for _, d := range prog.Decls {
		switch d := d.(type) {
		case *ast.ImportDecl:
			// Recursive import
			modName := d.Path
			alias := d.Alias
			if alias == "" {
				parts := splitPath(modName)
				alias = parts[len(parts)-1]
			}
			depEnv, err := ml.Resolve(modName)
			if err != nil {
				return nil, fmt.Errorf("line %d: %w", d.Line, err)
			}
			modEnv.Define(alias, value.Value{T: value.TypeModule, Data: depEnv})
		case *ast.FnDecl:
			fv := &value.FuncVal{Name: d.Name, Body: d.Body, Return: d.Return, Closure: modEnv}
			for _, p := range d.Params {
				fv.Params = append(fv.Params, value.FuncParam{Type: p.Type.Name, TypeExpr: p.Type, Name: p.Name})
			}
			modEnv.Define(d.Name, value.NewFunc(fv))
		case *ast.VarDecl:
			val, err := ml.interp.evalExpr(d.Init, modEnv)
			if err != nil {
				return nil, err
			}
			modEnv.Define(d.Name, val)
		case *ast.ClassDecl:
			cv := &value.ClassVal{
				Name:       d.Name,
				Implements: d.Implements,
				Methods:    make(map[string]*value.FuncVal),
				Closure:    modEnv,
			}
			for _, f := range d.Fields {
				cv.Fields = append(cv.Fields, value.ClassFieldDef{
					Name: f.Name, Type: f.Type.Name, Pub: f.Pub,
				})
			}
			for _, m := range d.Methods {
				fv := &value.FuncVal{Name: m.Name, Body: m.Body, Return: m.Return, Closure: modEnv}
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
				iface, ok := ml.interp.interfaces[ifaceName]
				if !ok {
					return nil, fmt.Errorf("line %d: class %s implements unknown interface %q", d.Line, d.Name, ifaceName)
				}
				for _, req := range iface.Methods {
					m, ok := cv.Methods[req.Name]
					if !ok {
						return nil, fmt.Errorf("line %d: class %s missing method %q required by interface %s", d.Line, d.Name, req.Name, ifaceName)
					}
					if len(m.Params) != len(req.ParamTypes) {
						return nil, fmt.Errorf("line %d: class %s method %s has %d params, interface %s requires %d", d.Line, d.Name, req.Name, len(m.Params), ifaceName, len(req.ParamTypes))
					}
				}
			}
			modEnv.Define(d.Name, value.NewClass(cv))
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
			ml.interp.interfaces[d.Name] = iv
		}
	}

	// Build exports: only pub symbols
	exports := NewEnv(nil)
	for _, d := range prog.Decls {
		switch d := d.(type) {
		case *ast.FnDecl:
			if d.Pub {
				if v, ok := modEnv.Get(d.Name); ok {
					exports.Define(d.Name, v)
				}
			}
		case *ast.VarDecl:
			if d.Pub {
				if v, ok := modEnv.Get(d.Name); ok {
					exports.Define(d.Name, v)
				}
			}
		case *ast.ClassDecl:
			if d.Pub {
				if v, ok := modEnv.Get(d.Name); ok {
					// Set module name for proper namespacing
					if v.T == value.TypeClass {
						cls := v.AsClass()
						cls.ModuleName = name
					}
					exports.Define(d.Name, v)
				}
			}
		}
	}

	ml.cache[name] = exports
	return exports, nil
}
