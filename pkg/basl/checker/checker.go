package checker

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/interp"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

type Diagnostic struct {
	Path    string
	Line    int
	Col     int
	Message string
}

func (d Diagnostic) String() string {
	if d.Path == "" {
		return d.Message
	}
	if d.Line > 0 && d.Col > 0 {
		return fmt.Sprintf("%s:%d:%d: %s", d.Path, d.Line, d.Col, d.Message)
	}
	if d.Line > 0 {
		return fmt.Sprintf("%s:%d: %s", d.Path, d.Line, d.Message)
	}
	return fmt.Sprintf("%s: %s", d.Path, d.Message)
}

type state int

const (
	stateNew state = iota
	stateLoading
	stateLoaded
)

type symbolKind int

const (
	symbolVar symbolKind = iota
	symbolConst
	symbolFn
	symbolClass
	symbolInterface
	symbolEnum
	symbolModule
)

type symbol struct {
	kind    symbolKind
	name    string
	typ     *ast.TypeExpr
	fn      *funcSig
	class   *classInfo
	iface   *interfaceInfo
	module  *moduleInfo
	builtin bool
	line    int
}

type funcSig struct {
	name   string
	params []*ast.TypeExpr
	ret    []*ast.TypeExpr
	// hasArityBounds means minArgs/maxArgs should be used instead of the default
	// arity derived from params and variadic.
	hasArityBounds bool
	minArgs        int
	// maxArgs is -1 when there is no upper bound.
	maxArgs int
	// variadic means the final parameter type repeats for any remaining args.
	variadic bool
	line     int
}

func (sig *funcSig) arityBounds() (int, int) {
	if sig == nil {
		return 0, 0
	}
	if sig.hasArityBounds {
		return sig.minArgs, sig.maxArgs
	}
	if sig.variadic {
		if len(sig.params) == 0 {
			return 0, -1
		}
		return len(sig.params) - 1, -1
	}
	return len(sig.params), len(sig.params)
}

func (sig *funcSig) paramType(argIdx int) *ast.TypeExpr {
	if sig == nil || len(sig.params) == 0 {
		return nil
	}
	if sig.variadic && argIdx >= len(sig.params)-1 {
		return sig.params[len(sig.params)-1]
	}
	if argIdx < 0 || argIdx >= len(sig.params) {
		return nil
	}
	return sig.params[argIdx]
}

type classInfo struct {
	name       string
	fields     map[string]*ast.TypeExpr
	methods    map[string]*funcSig
	init       *funcSig
	implements []string
	line       int
}

type interfaceInfo struct {
	name    string
	methods map[string]*funcSig
	line    int
}

type moduleInfo struct {
	path       string
	prog       *ast.Program
	symbols    map[string]*symbol
	imports    map[string]*symbol
	exports    map[string]*symbol
	strict     bool
	state      state
	validated  bool
	checking   bool
	cycleStack []string
}

type scope struct {
	vars   map[string]*ast.TypeExpr
	parent *scope
}

func newScope(parent *scope) *scope {
	return &scope{
		vars:   make(map[string]*ast.TypeExpr),
		parent: parent,
	}
}

func (s *scope) define(name string, typ *ast.TypeExpr) {
	s.vars[name] = typ
}

func (s *scope) get(name string) (*ast.TypeExpr, bool) {
	if typ, ok := s.vars[name]; ok {
		return typ, true
	}
	if s.parent != nil {
		return s.parent.get(name)
	}
	return nil, false
}

type exprInfo struct {
	returns []*ast.TypeExpr
	sym     *symbol
}

type bodyContext struct {
	mod          *moduleInfo
	scope        *scope
	currentClass *classInfo
	returns      []*ast.TypeExpr
}

type Checker struct {
	searchPaths []string
	builtin     map[string]*moduleInfo
	modules     map[string]*moduleInfo
	diagnostics []Diagnostic
	overlays    map[string]string
}

func CheckFile(path string, searchPaths []string) ([]Diagnostic, error) {
	return CheckFileWithOverlays(path, searchPaths, nil)
}

func CheckFileWithOverlays(path string, searchPaths []string, overlays map[string]string) ([]Diagnostic, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}

	c := &Checker{
		searchPaths: append([]string(nil), searchPaths...),
		builtin:     make(map[string]*moduleInfo),
		modules:     make(map[string]*moduleInfo),
		overlays:    normalizeOverlays(overlays),
	}
	for _, name := range interp.BuiltinModuleNames() {
		c.builtin[name] = newBuiltinModule(name)
	}
	c.builtin["test"] = newBuiltinModule("test")

	c.checkModule(absPath, nil)

	sort.Slice(c.diagnostics, func(i, j int) bool {
		if c.diagnostics[i].Path != c.diagnostics[j].Path {
			return c.diagnostics[i].Path < c.diagnostics[j].Path
		}
		if c.diagnostics[i].Line != c.diagnostics[j].Line {
			return c.diagnostics[i].Line < c.diagnostics[j].Line
		}
		if c.diagnostics[i].Col != c.diagnostics[j].Col {
			return c.diagnostics[i].Col < c.diagnostics[j].Col
		}
		return c.diagnostics[i].Message < c.diagnostics[j].Message
	})

	return c.diagnostics, nil
}

func (c *Checker) addDiag(path string, line, col int, format string, args ...any) {
	c.diagnostics = append(c.diagnostics, Diagnostic{
		Path:    path,
		Line:    line,
		Col:     col,
		Message: fmt.Sprintf(format, args...),
	})
}

func (c *Checker) checkModule(absPath string, stack []string) *moduleInfo {
	absPath, err := filepath.Abs(absPath)
	if err != nil {
		c.addDiag(absPath, 0, 0, "%s", err)
		return nil
	}

	mod, ok := c.modules[absPath]
	if ok {
		switch mod.state {
		case stateLoading:
			cycle := append(append([]string(nil), stack...), absPath)
			c.addDiag(absPath, 0, 0, "import cycle detected: %s", strings.Join(shortPaths(cycle), " -> "))
			return mod
		case stateLoaded:
			if !mod.validated {
				c.validateModule(mod)
			}
			return mod
		}
	}

	src, err := c.readFile(absPath)
	if err != nil {
		c.addDiag(absPath, 0, 0, "%s", err)
		return nil
	}

	tokens, err := lexer.New(string(src)).Tokenize()
	if err != nil {
		c.addDiag(absPath, 0, 0, "%s", err)
		return nil
	}

	prog, err := parser.New(tokens).Parse()
	if err != nil {
		c.addDiag(absPath, 0, 0, "%s", err)
		return nil
	}

	mod = &moduleInfo{
		path:    absPath,
		prog:    prog,
		symbols: make(map[string]*symbol),
		imports: make(map[string]*symbol),
		exports: make(map[string]*symbol),
		state:   stateLoading,
	}
	c.modules[absPath] = mod

	c.collectImports(mod, append(append([]string(nil), stack...), absPath))
	c.collectDecls(mod)
	c.validateModule(mod)
	mod.state = stateLoaded
	return mod
}

func (c *Checker) collectImports(mod *moduleInfo, stack []string) {
	for _, decl := range mod.prog.Decls {
		imp, ok := decl.(*ast.ImportDecl)
		if !ok {
			continue
		}

		alias := imp.Alias
		if alias == "" {
			parts := strings.Split(imp.Path, "/")
			alias = parts[len(parts)-1]
		}
		if c.hasNameConflict(mod, alias) {
			c.addDiag(mod.path, imp.Line, 0, "duplicate name %q", alias)
			continue
		}

		if builtinMod, ok := c.builtin[imp.Path]; ok {
			sym := &symbol{
				kind:    symbolModule,
				name:    alias,
				module:  builtinMod,
				builtin: true,
				line:    imp.Line,
			}
			mod.imports[alias] = sym
			mod.symbols[alias] = sym
			continue
		}

		resolved, err := c.resolveImport(imp.Path)
		if err != nil {
			c.addDiag(mod.path, imp.Line, 0, "module %q not found", imp.Path)
			continue
		}

		dep := c.checkModule(resolved, stack)
		sym := &symbol{
			kind:   symbolModule,
			name:   alias,
			module: dep,
			line:   imp.Line,
		}
		mod.imports[alias] = sym
		mod.symbols[alias] = sym
	}
}

func (c *Checker) collectDecls(mod *moduleInfo) {
	for _, decl := range mod.prog.Decls {
		switch d := decl.(type) {
		case *ast.ImportDecl:
			continue
		case *ast.FnDecl:
			if c.hasNameConflict(mod, d.Name) {
				c.addDiag(mod.path, d.Line, 0, "duplicate name %q", d.Name)
				continue
			}
			sym := &symbol{
				kind: symbolFn,
				name: d.Name,
				fn:   fnSigFromDecl(d),
				line: d.Line,
			}
			mod.symbols[d.Name] = sym
			if d.Pub {
				mod.exports[d.Name] = sym
			}
		case *ast.VarDecl:
			if c.hasNameConflict(mod, d.Name) {
				c.addDiag(mod.path, d.Line, 0, "duplicate name %q", d.Name)
				continue
			}
			sym := &symbol{
				kind: symbolVar,
				name: d.Name,
				typ:  d.Type,
				line: d.Line,
			}
			mod.symbols[d.Name] = sym
			if d.Pub {
				mod.exports[d.Name] = sym
			}
		case *ast.ConstDecl:
			if c.hasNameConflict(mod, d.Name) {
				c.addDiag(mod.path, d.Line, 0, "duplicate name %q", d.Name)
				continue
			}
			sym := &symbol{
				kind: symbolConst,
				name: d.Name,
				typ:  d.Type,
				line: d.Line,
			}
			mod.symbols[d.Name] = sym
			if d.Pub {
				mod.exports[d.Name] = sym
			}
		case *ast.EnumDecl:
			if c.hasNameConflict(mod, d.Name) {
				c.addDiag(mod.path, d.Line, 0, "duplicate name %q", d.Name)
				continue
			}
			sym := &symbol{
				kind: symbolEnum,
				name: d.Name,
				line: d.Line,
			}
			mod.symbols[d.Name] = sym
			if d.Pub {
				mod.exports[d.Name] = sym
			}
		case *ast.InterfaceDecl:
			if c.hasNameConflict(mod, d.Name) {
				c.addDiag(mod.path, d.Line, 0, "duplicate name %q", d.Name)
				continue
			}
			iface := &interfaceInfo{
				name:    d.Name,
				methods: make(map[string]*funcSig),
				line:    d.Line,
			}
			for _, m := range d.Methods {
				if _, exists := iface.methods[m.Name]; exists {
					c.addDiag(mod.path, m.Line, 0, "duplicate interface method %q on %s", m.Name, d.Name)
					continue
				}
				iface.methods[m.Name] = fnSigFromInterfaceMethod(m)
			}
			sym := &symbol{
				kind:  symbolInterface,
				name:  d.Name,
				iface: iface,
				line:  d.Line,
			}
			mod.symbols[d.Name] = sym
			if d.Pub {
				mod.exports[d.Name] = sym
			}
		case *ast.ClassDecl:
			if c.hasNameConflict(mod, d.Name) {
				c.addDiag(mod.path, d.Line, 0, "duplicate name %q", d.Name)
				continue
			}
			class := &classInfo{
				name:       d.Name,
				fields:     make(map[string]*ast.TypeExpr),
				methods:    make(map[string]*funcSig),
				implements: append([]string(nil), d.Implements...),
				line:       d.Line,
			}
			for _, f := range d.Fields {
				if _, exists := class.fields[f.Name]; exists {
					c.addDiag(mod.path, f.Line, 0, "duplicate field %q on class %s", f.Name, d.Name)
					continue
				}
				class.fields[f.Name] = f.Type
			}
			for _, m := range d.Methods {
				sig := fnSigFromDecl(m)
				if m.Name == "init" {
					if class.init != nil {
						c.addDiag(mod.path, m.Line, 0, "duplicate init method on class %s", d.Name)
						continue
					}
					class.init = sig
					continue
				}
				if _, exists := class.methods[m.Name]; exists {
					c.addDiag(mod.path, m.Line, 0, "duplicate method %q on class %s", m.Name, d.Name)
					continue
				}
				class.methods[m.Name] = sig
			}
			sym := &symbol{
				kind:  symbolClass,
				name:  d.Name,
				class: class,
				line:  d.Line,
			}
			mod.symbols[d.Name] = sym
			if d.Pub {
				mod.exports[d.Name] = sym
			}
		}
	}
}

func (c *Checker) validateModule(mod *moduleInfo) {
	if mod == nil || mod.validated || mod.checking {
		return
	}
	mod.checking = true
	defer func() {
		mod.checking = false
		mod.validated = true
	}()

	for _, decl := range mod.prog.Decls {
		switch d := decl.(type) {
		case *ast.VarDecl:
			c.checkModuleValueInit(mod, d.Type, d.Init, d.Line, "variable "+d.Name)
		case *ast.ConstDecl:
			c.checkModuleValueInit(mod, d.Type, d.Init, d.Line, "constant "+d.Name)
		case *ast.ClassDecl:
			c.validateClass(mod, d)
		case *ast.FnDecl:
			c.checkFunctionBody(mod, nil, d)
		}
	}
}

func (c *Checker) validateClass(mod *moduleInfo, decl *ast.ClassDecl) {
	sym := mod.symbols[decl.Name]
	if sym == nil || sym.class == nil {
		return
	}

	class := sym.class
	for _, ifaceName := range class.implements {
		ifaceSym := mod.symbols[ifaceName]
		if ifaceSym == nil || ifaceSym.kind != symbolInterface || ifaceSym.iface == nil {
			c.addDiag(mod.path, decl.Line, 0, "class %s implements unknown interface %q", decl.Name, ifaceName)
			continue
		}

		for name, req := range ifaceSym.iface.methods {
			got, ok := class.methods[name]
			if !ok {
				c.addDiag(mod.path, decl.Line, 0, "class %s missing method %q required by interface %s", decl.Name, name, ifaceName)
				continue
			}
			if len(got.params) != len(req.params) {
				c.addDiag(mod.path, got.line, 0, "class %s method %s has %d params, interface %s requires %d", decl.Name, name, len(got.params), ifaceName, len(req.params))
				continue
			}
			for i := range req.params {
				if !sameType(got.params[i], req.params[i]) {
					c.addDiag(mod.path, got.line, 0, "class %s method %s param %d has type %s, interface %s requires %s", decl.Name, name, i+1, typeString(got.params[i]), ifaceName, typeString(req.params[i]))
				}
			}
			if len(got.ret) != len(req.ret) {
				c.addDiag(mod.path, got.line, 0, "class %s method %s returns %d values, interface %s requires %d", decl.Name, name, len(got.ret), ifaceName, len(req.ret))
				continue
			}
			for i := range req.ret {
				if !sameType(got.ret[i], req.ret[i]) {
					c.addDiag(mod.path, got.line, 0, "class %s method %s return %d has type %s, interface %s requires %s", decl.Name, name, i+1, typeString(got.ret[i]), ifaceName, typeString(req.ret[i]))
				}
			}
		}
	}

	for _, method := range decl.Methods {
		c.checkFunctionBody(mod, class, method)
	}
}

func (c *Checker) checkModuleValueInit(mod *moduleInfo, declared *ast.TypeExpr, init ast.Expr, line int, what string) {
	info := c.checkExpr(&bodyContext{
		mod:   mod,
		scope: newScope(nil),
	}, init)

	if len(info.returns) > 1 {
		c.addDiag(mod.path, line, 0, "%s expects a single value, but the expression returns %d values", what, len(info.returns))
		return
	}
	if len(info.returns) == 1 && declared != nil && info.returns[0] != nil && !c.isAssignable(mod, declared, info.returns[0]) {
		c.addDiag(mod.path, line, 0, "type mismatch in %s: expected %s, received %s", what, typeString(declared), typeString(info.returns[0]))
	}
}

func (c *Checker) checkFunctionBody(mod *moduleInfo, class *classInfo, decl *ast.FnDecl) {
	ctx := &bodyContext{
		mod:          mod,
		scope:        newScope(nil),
		currentClass: class,
		returns:      returnTypes(decl.Return),
	}
	if class != nil {
		ctx.scope.define("self", &ast.TypeExpr{Name: class.name})
	}
	for _, p := range decl.Params {
		ctx.scope.define(p.Name, p.Type)
	}
	c.checkBlock(ctx, decl.Body)
	if len(ctx.returns) > 0 && !c.blockAlwaysReturns(decl.Body) {
		c.addDiag(mod.path, decl.Line, 0, "function %s may exit without returning %d values", decl.Name, len(ctx.returns))
	}
}

func (c *Checker) checkBlock(ctx *bodyContext, block *ast.Block) {
	if block == nil {
		return
	}
	parent := ctx.scope
	ctx.scope = newScope(parent)
	defer func() {
		ctx.scope = parent
	}()

	for _, stmt := range block.Stmts {
		c.checkStmt(ctx, stmt)
	}
}

func (c *Checker) checkStmt(ctx *bodyContext, stmt ast.Stmt) {
	switch s := stmt.(type) {
	case *ast.Block:
		c.checkBlock(ctx, s)
	case *ast.VarStmt:
		info := c.checkExpr(ctx, s.Init)
		if len(info.returns) > 1 {
			c.addDiag(ctx.mod.path, s.Line, 0, "variable %s expects a single value, but the expression returns %d values", s.Name, len(info.returns))
		} else if len(info.returns) == 1 && s.Type != nil && info.returns[0] != nil && !c.isAssignable(ctx.mod, s.Type, info.returns[0]) {
			c.addDiag(ctx.mod.path, s.Line, 0, "type mismatch in variable %s: expected %s, received %s", s.Name, typeString(s.Type), typeString(info.returns[0]))
		}
		ctx.scope.define(s.Name, s.Type)
	case *ast.TupleBindStmt:
		c.checkTupleBindings(ctx, s.Bindings, s.Value, s.Line)
	case *ast.GuardStmt:
		if len(s.Bindings) == 0 {
			c.addDiag(ctx.mod.path, s.Line, 0, "guard requires at least one binding")
			return
		}
		last := s.Bindings[len(s.Bindings)-1]
		if last.Type == nil || last.Type.Name != "err" {
			c.addDiag(ctx.mod.path, s.Line, 0, "guard requires the final binding to be err")
		} else if last.Discard {
			c.addDiag(ctx.mod.path, s.Line, 0, "guard requires a named err binding")
		}
		c.checkTupleBindings(ctx, s.Bindings, s.Value, s.Line)
		c.checkBlock(ctx, s.Body)
	case *ast.AssignStmt:
		target := c.checkExpr(ctx, s.Target)
		value := c.checkExpr(ctx, s.Value)
		if len(value.returns) > 1 {
			c.addDiag(ctx.mod.path, s.Line, 0, "assignment expects a single value, but the expression returns %d values", len(value.returns))
			return
		}
		if len(target.returns) == 1 && len(value.returns) == 1 && target.returns[0] != nil && value.returns[0] != nil && !c.isAssignable(ctx.mod, target.returns[0], value.returns[0]) {
			c.addDiag(ctx.mod.path, s.Line, 0, "type mismatch in assignment: expected %s, received %s", typeString(target.returns[0]), typeString(value.returns[0]))
		}
	case *ast.CompoundAssignStmt:
		c.checkExpr(ctx, s.Target)
		value := c.checkExpr(ctx, s.Value)
		if len(value.returns) > 1 {
			c.addDiag(ctx.mod.path, s.Line, 0, "compound assignment expects a single value, but the expression returns %d values", len(value.returns))
		}
	case *ast.IncDecStmt:
		c.checkExpr(ctx, s.Target)
	case *ast.ExprStmt:
		info := c.checkExpr(ctx, s.Expr)
		if len(info.returns) > 1 {
			c.addDiag(ctx.mod.path, s.Line, 0, "expression statement expects a single value, but the expression returns %d values", len(info.returns))
		}
	case *ast.IfStmt:
		cond := c.checkExpr(ctx, s.Cond)
		if len(cond.returns) == 1 && cond.returns[0] != nil && cond.returns[0].Name != "bool" {
			c.addDiag(ctx.mod.path, s.Line, 0, "if condition must be bool, received %s", typeString(cond.returns[0]))
		}
		c.checkBlock(ctx, s.Then)
		if s.Else != nil {
			c.checkStmt(ctx, s.Else)
		}
	case *ast.WhileStmt:
		cond := c.checkExpr(ctx, s.Cond)
		if len(cond.returns) == 1 && cond.returns[0] != nil && cond.returns[0].Name != "bool" {
			c.addDiag(ctx.mod.path, s.Line, 0, "while condition must be bool, received %s", typeString(cond.returns[0]))
		}
		c.checkBlock(ctx, s.Body)
	case *ast.ForStmt:
		if s.Init != nil {
			c.checkStmt(ctx, s.Init)
		}
		cond := c.checkExpr(ctx, s.Cond)
		if len(cond.returns) == 1 && cond.returns[0] != nil && cond.returns[0].Name != "bool" {
			c.addDiag(ctx.mod.path, s.Line, 0, "for condition must be bool, received %s", typeString(cond.returns[0]))
		}
		if s.Post != nil {
			c.checkStmt(ctx, s.Post)
		}
		c.checkBlock(ctx, s.Body)
	case *ast.ForInStmt:
		iterInfo := c.checkExpr(ctx, s.Iter)
		loopScope := &bodyContext{
			mod:          ctx.mod,
			scope:        newScope(ctx.scope),
			currentClass: ctx.currentClass,
			returns:      ctx.returns,
		}
		if s.KeyName != "" {
			loopScope.scope.define(s.KeyName, nil)
		}
		loopScope.scope.define(s.ValName, nil)
		if len(iterInfo.returns) == 1 && iterInfo.returns[0] != nil {
			iterType := iterInfo.returns[0]
			if iterType.Name == "array" {
				loopScope.scope.define(s.ValName, iterType.ElemType)
			} else if iterType.Name == "map" {
				if s.KeyName != "" {
					loopScope.scope.define(s.KeyName, iterType.KeyType)
				}
				loopScope.scope.define(s.ValName, iterType.ValType)
			} else {
				c.addDiag(ctx.mod.path, s.Line, 0, "for-in expects array or map, received %s", typeString(iterType))
			}
		}
		c.checkBlock(loopScope, s.Body)
	case *ast.SwitchStmt:
		tag := c.checkExpr(ctx, s.Tag)
		for _, cs := range s.Cases {
			for _, val := range cs.Values {
				valInfo := c.checkExpr(ctx, val)
				if len(cs.Values) == 0 {
					continue
				}
				if len(tag.returns) == 1 && len(valInfo.returns) == 1 && tag.returns[0] != nil && valInfo.returns[0] != nil && !c.isAssignable(ctx.mod, tag.returns[0], valInfo.returns[0]) {
					c.addDiag(ctx.mod.path, s.Line, 0, "switch case expects %s, received %s", typeString(tag.returns[0]), typeString(valInfo.returns[0]))
				}
			}
			for _, st := range cs.Body {
				c.checkStmt(ctx, st)
			}
		}
	case *ast.ReturnStmt:
		c.checkReturn(ctx, s)
	case *ast.DeferStmt:
		info := c.checkExpr(ctx, s.Call)
		if info.sym == nil && !isDirectCallableType(info.returns) {
			c.addDiag(ctx.mod.path, s.Line, 0, "defer requires a function or method call")
		}
	case *ast.BreakStmt, *ast.ContinueStmt:
		return
	}
}

func (c *Checker) checkTupleBindings(ctx *bodyContext, bindings []ast.TupleBindItem, valueExpr ast.Expr, line int) {
	info := c.checkExpr(ctx, valueExpr)
	if len(info.returns) == 0 {
		return
	}
	if len(info.returns) != len(bindings) {
		c.addDiag(ctx.mod.path, line, 0, "tuple binding expects %d values, got %d", len(bindings), len(info.returns))
		return
	}
	for i, binding := range bindings {
		if binding.Discard {
			continue
		}
		if info.returns[i] != nil && !c.isAssignable(ctx.mod, binding.Type, info.returns[i]) {
			c.addDiag(ctx.mod.path, line, 0, "tuple binding %s expects %s, received %s", binding.Name, typeString(binding.Type), typeString(info.returns[i]))
		}
		ctx.scope.define(binding.Name, binding.Type)
	}
}

func (c *Checker) blockAlwaysReturns(block *ast.Block) bool {
	if block == nil {
		return false
	}
	for _, stmt := range block.Stmts {
		if c.stmtAlwaysReturns(stmt) {
			return true
		}
	}
	return false
}

func (c *Checker) stmtAlwaysReturns(stmt ast.Stmt) bool {
	switch s := stmt.(type) {
	case *ast.Block:
		return c.blockAlwaysReturns(s)
	case *ast.ReturnStmt:
		return true
	case *ast.IfStmt:
		if s.Else == nil {
			return false
		}
		return c.blockAlwaysReturns(s.Then) && c.stmtAlwaysReturns(s.Else)
	case *ast.SwitchStmt:
		if len(s.Cases) == 0 {
			return false
		}
		hasDefault := false
		for _, cs := range s.Cases {
			if len(cs.Values) == 0 {
				hasDefault = true
			}
			if !c.stmtsAlwaysReturn(cs.Body) {
				return false
			}
		}
		return hasDefault
	default:
		return false
	}
}

func (c *Checker) stmtsAlwaysReturn(stmts []ast.Stmt) bool {
	for _, stmt := range stmts {
		if c.stmtAlwaysReturns(stmt) {
			return true
		}
	}
	return false
}

func (c *Checker) checkReturn(ctx *bodyContext, stmt *ast.ReturnStmt) {
	actual := flattenReturnExprs(c, ctx, stmt.Values)
	if len(ctx.returns) != len(actual) {
		c.addDiag(ctx.mod.path, stmt.Line, 0, "return expects %d values, got %d", len(ctx.returns), len(actual))
		return
	}
	for i := range ctx.returns {
		if ctx.returns[i] != nil && actual[i] != nil && !c.isAssignable(ctx.mod, ctx.returns[i], actual[i]) {
			c.addDiag(ctx.mod.path, stmt.Line, 0, "return value %d expects %s, received %s", i+1, typeString(ctx.returns[i]), typeString(actual[i]))
		}
	}
}

func flattenReturnExprs(c *Checker, ctx *bodyContext, values []ast.Expr) []*ast.TypeExpr {
	if len(values) == 0 {
		return nil
	}
	if len(values) == 1 {
		if tuple, ok := values[0].(*ast.TupleExpr); ok {
			var out []*ast.TypeExpr
			for _, elem := range tuple.Elems {
				info := c.checkExpr(ctx, elem)
				if len(info.returns) == 0 {
					out = append(out, nil)
					continue
				}
				out = append(out, info.returns[0])
			}
			return out
		}
		info := c.checkExpr(ctx, values[0])
		if len(info.returns) == 0 {
			return []*ast.TypeExpr{nil}
		}
		return info.returns
	}

	var out []*ast.TypeExpr
	for _, expr := range values {
		info := c.checkExpr(ctx, expr)
		if len(info.returns) == 0 {
			out = append(out, nil)
			continue
		}
		if len(info.returns) == 1 {
			out = append(out, info.returns[0])
			continue
		}
		out = append(out, info.returns...)
	}
	return out
}

func (c *Checker) checkExpr(ctx *bodyContext, expr ast.Expr) exprInfo {
	switch e := expr.(type) {
	case *ast.IntLit:
		return exprInfo{returns: []*ast.TypeExpr{{Name: "i32"}}}
	case *ast.FloatLit:
		return exprInfo{returns: []*ast.TypeExpr{{Name: "f64"}}}
	case *ast.StringLit:
		return exprInfo{returns: []*ast.TypeExpr{{Name: "string"}}}
	case *ast.BoolLit:
		return exprInfo{returns: []*ast.TypeExpr{{Name: "bool"}}}
	case *ast.FStringExpr:
		for _, part := range e.Parts {
			if part.IsExpr {
				c.checkExpr(ctx, part.Expr)
			}
		}
		return exprInfo{returns: []*ast.TypeExpr{{Name: "string"}}}
	case *ast.Ident:
		return c.identInfo(ctx, e)
	case *ast.SelfExpr:
		if ctx.currentClass == nil {
			c.addDiag(ctx.mod.path, e.Line, 0, "self is only valid inside class methods")
			return exprInfo{}
		}
		return exprInfo{returns: []*ast.TypeExpr{{Name: ctx.currentClass.name}}}
	case *ast.MemberExpr:
		return c.memberInfo(ctx, e)
	case *ast.CallExpr:
		return c.callInfo(ctx, e)
	case *ast.TypeConvExpr:
		arg := c.checkExpr(ctx, e.Arg)
		if len(arg.returns) == 1 && arg.returns[0] != nil && !isSupportedTypeConversion(e.Target.Name, arg.returns[0].Name) {
			if arg.returns[0].Name == "string" {
				if parseFn := parseFuncForTypeName(e.Target.Name); parseFn != "" {
					c.addDiag(ctx.mod.path, e.Line, 0, "cannot convert string to %s; use %s(...) for string parsing", e.Target.Name, parseFn)
				} else {
					c.addDiag(ctx.mod.path, e.Line, 0, "cannot convert %s to %s", typeString(arg.returns[0]), e.Target.Name)
				}
			} else {
				c.addDiag(ctx.mod.path, e.Line, 0, "cannot convert %s to %s", typeString(arg.returns[0]), e.Target.Name)
			}
		}
		return exprInfo{returns: []*ast.TypeExpr{e.Target}}
	case *ast.TupleExpr:
		var out []*ast.TypeExpr
		for _, elem := range e.Elems {
			info := c.checkExpr(ctx, elem)
			if len(info.returns) == 0 {
				out = append(out, nil)
				continue
			}
			out = append(out, info.returns[0])
		}
		return exprInfo{returns: out}
	case *ast.ErrExpr:
		c.checkExpr(ctx, e.Msg)
		c.checkExpr(ctx, e.Kind)
		return exprInfo{returns: []*ast.TypeExpr{{Name: "err"}}}
	case *ast.FnLitExpr:
		c.checkFunctionBody(ctx.mod, nil, e.Decl)
		return exprInfo{returns: []*ast.TypeExpr{fnTypeFromSig(fnSigFromDecl(e.Decl))}}
	case *ast.UnaryExpr:
		operand := c.checkExpr(ctx, e.Operand)
		switch e.Op {
		case "!":
			if len(operand.returns) == 1 && operand.returns[0] != nil && operand.returns[0].Name != "bool" {
				c.addDiag(ctx.mod.path, e.Line, 0, "operator ! expects bool, received %s", typeString(operand.returns[0]))
			}
			return exprInfo{returns: []*ast.TypeExpr{{Name: "bool"}}}
		case "-":
			if len(operand.returns) == 1 && operand.returns[0] != nil && !isNumericTypeName(operand.returns[0].Name) {
				c.addDiag(ctx.mod.path, e.Line, 0, "operator - expects numeric operand, received %s", typeString(operand.returns[0]))
			}
			return exprInfo{returns: operand.returns}
		default:
			return exprInfo{returns: operand.returns}
		}
	case *ast.BinaryExpr:
		left := c.checkExpr(ctx, e.Left)
		right := c.checkExpr(ctx, e.Right)
		switch e.Op {
		case "&&", "||":
			if len(left.returns) == 1 && left.returns[0] != nil && left.returns[0].Name != "bool" {
				c.addDiag(ctx.mod.path, e.Line, 0, "operator %s expects bool operands, left side is %s", e.Op, typeString(left.returns[0]))
			}
			if len(right.returns) == 1 && right.returns[0] != nil && right.returns[0].Name != "bool" {
				c.addDiag(ctx.mod.path, e.Line, 0, "operator %s expects bool operands, right side is %s", e.Op, typeString(right.returns[0]))
			}
			return exprInfo{returns: []*ast.TypeExpr{{Name: "bool"}}}
		case "==", "!=":
			return exprInfo{returns: []*ast.TypeExpr{{Name: "bool"}}}
		case "<", ">", "<=", ">=":
			if len(left.returns) == 1 && len(right.returns) == 1 && left.returns[0] != nil && right.returns[0] != nil {
				if !c.isAssignable(ctx.mod, left.returns[0], right.returns[0]) && !c.isAssignable(ctx.mod, right.returns[0], left.returns[0]) {
					c.addDiag(ctx.mod.path, e.Line, 0, "operator %s compares incompatible types %s and %s", e.Op, typeString(left.returns[0]), typeString(right.returns[0]))
				}
			}
			return exprInfo{returns: []*ast.TypeExpr{{Name: "bool"}}}
		case "+":
			if len(left.returns) == 1 && len(right.returns) == 1 && left.returns[0] != nil && right.returns[0] != nil {
				if left.returns[0].Name == "string" && right.returns[0].Name == "string" {
					return exprInfo{returns: []*ast.TypeExpr{{Name: "string"}}}
				}
				if isNumericTypeName(left.returns[0].Name) && sameType(left.returns[0], right.returns[0]) {
					return exprInfo{returns: left.returns}
				}
				c.addDiag(ctx.mod.path, e.Line, 0, "operator + expects matching numeric types or strings, received %s and %s", typeString(left.returns[0]), typeString(right.returns[0]))
			}
		case "-", "*", "/", "%":
			if len(left.returns) == 1 && len(right.returns) == 1 && left.returns[0] != nil && right.returns[0] != nil {
				if isNumericTypeName(left.returns[0].Name) && sameType(left.returns[0], right.returns[0]) {
					return exprInfo{returns: left.returns}
				}
				c.addDiag(ctx.mod.path, e.Line, 0, "operator %s expects matching numeric types, received %s and %s", e.Op, typeString(left.returns[0]), typeString(right.returns[0]))
			}
		}
		return exprInfo{returns: left.returns}
	case *ast.TernaryExpr:
		cond := c.checkExpr(ctx, e.Condition)
		if len(cond.returns) == 1 && cond.returns[0] != nil && cond.returns[0].Name != "bool" {
			c.addDiag(ctx.mod.path, e.Line, 0, "ternary condition must be bool, received %s", typeString(cond.returns[0]))
		}
		trueInfo := c.checkExpr(ctx, e.TrueExpr)
		falseInfo := c.checkExpr(ctx, e.FalseExpr)
		if len(trueInfo.returns) == 1 && len(falseInfo.returns) == 1 && sameType(trueInfo.returns[0], falseInfo.returns[0]) {
			return exprInfo{returns: trueInfo.returns}
		}
		return exprInfo{}
	case *ast.ArrayLit:
		var elemType *ast.TypeExpr
		consistent := true
		for _, elem := range e.Elems {
			info := c.checkExpr(ctx, elem)
			if len(info.returns) != 1 || info.returns[0] == nil {
				consistent = false
				continue
			}
			if elemType == nil {
				elemType = info.returns[0]
				continue
			}
			if !sameType(elemType, info.returns[0]) {
				consistent = false
			}
		}
		if !consistent {
			elemType = nil
		}
		return exprInfo{returns: []*ast.TypeExpr{{Name: "array", ElemType: elemType}}}
	case *ast.MapLit:
		var keyType *ast.TypeExpr
		var valType *ast.TypeExpr
		keysConsistent := true
		valsConsistent := true
		for _, key := range e.Keys {
			info := c.checkExpr(ctx, key)
			if len(info.returns) == 1 && info.returns[0] != nil {
				if keyType == nil {
					keyType = info.returns[0]
				} else if !sameType(keyType, info.returns[0]) {
					keysConsistent = false
				}
			} else {
				keysConsistent = false
			}
		}
		for _, val := range e.Values {
			info := c.checkExpr(ctx, val)
			if len(info.returns) == 1 && info.returns[0] != nil {
				if valType == nil {
					valType = info.returns[0]
				} else if !sameType(valType, info.returns[0]) {
					valsConsistent = false
				}
			} else {
				valsConsistent = false
			}
		}
		if !keysConsistent {
			keyType = nil
		}
		if !valsConsistent {
			valType = nil
		}
		return exprInfo{returns: []*ast.TypeExpr{{Name: "map", KeyType: keyType, ValType: valType}}}
	case *ast.IndexExpr:
		objInfo := c.checkExpr(ctx, e.Object)
		idxInfo := c.checkExpr(ctx, e.Index)
		if len(objInfo.returns) == 1 && objInfo.returns[0] != nil {
			switch objInfo.returns[0].Name {
			case "array":
				if len(idxInfo.returns) == 1 && idxInfo.returns[0] != nil && idxInfo.returns[0].Name != "i32" {
					c.addDiag(ctx.mod.path, e.Line, 0, "array index must be i32, received %s", typeString(idxInfo.returns[0]))
				}
				return exprInfo{returns: []*ast.TypeExpr{objInfo.returns[0].ElemType}}
			case "map":
				if len(idxInfo.returns) == 1 && idxInfo.returns[0] != nil && objInfo.returns[0].KeyType != nil && !c.isAssignable(ctx.mod, objInfo.returns[0].KeyType, idxInfo.returns[0]) {
					c.addDiag(ctx.mod.path, e.Line, 0, "map index expects %s, received %s", typeString(objInfo.returns[0].KeyType), typeString(idxInfo.returns[0]))
				}
				return exprInfo{returns: []*ast.TypeExpr{objInfo.returns[0].ValType}}
			default:
				c.addDiag(ctx.mod.path, e.Line, 0, "type %s is not indexable", typeString(objInfo.returns[0]))
			}
		}
		return exprInfo{}
	default:
		return exprInfo{}
	}
}

func (c *Checker) identInfo(ctx *bodyContext, ident *ast.Ident) exprInfo {
	if typ, ok := ctx.scope.get(ident.Name); ok {
		return exprInfo{returns: []*ast.TypeExpr{typ}}
	}

	if ident.Name == "ok" {
		return exprInfo{returns: []*ast.TypeExpr{{Name: "err"}}}
	}
	if ident.Name == "err" {
		return exprInfo{
			sym: &symbol{
				kind:   symbolModule,
				name:   "err",
				module: nil,
			},
		}
	}

	if sym, ok := ctx.mod.symbols[ident.Name]; ok {
		switch sym.kind {
		case symbolVar, symbolConst:
			return exprInfo{returns: []*ast.TypeExpr{sym.typ}, sym: sym}
		case symbolFn:
			return exprInfo{returns: []*ast.TypeExpr{fnTypeFromSig(sym.fn)}, sym: sym}
		default:
			return exprInfo{sym: sym}
		}
	}

	c.addDiag(ctx.mod.path, ident.Line, 0, "unknown identifier %q", ident.Name)
	return exprInfo{}
}

func (c *Checker) memberInfo(ctx *bodyContext, expr *ast.MemberExpr) exprInfo {
	obj := c.checkExpr(ctx, expr.Object)

	if obj.sym != nil {
		switch obj.sym.kind {
		case symbolModule:
			if obj.sym.module == nil {
				if isErrKind(expr.Field) {
					return exprInfo{returns: []*ast.TypeExpr{{Name: "string"}}}
				}
				return exprInfo{}
			}
			member, ok := obj.sym.module.exports[expr.Field]
			if !ok {
				if !obj.sym.builtin || obj.sym.module.strict {
					c.addDiag(ctx.mod.path, expr.Line, 0, "module member %q not found", expr.Field)
				}
				return exprInfo{}
			}
			switch member.kind {
			case symbolVar, symbolConst:
				return exprInfo{returns: []*ast.TypeExpr{member.typ}, sym: member}
			case symbolFn:
				return exprInfo{returns: []*ast.TypeExpr{fnTypeFromSig(member.fn)}, sym: member}
			default:
				return exprInfo{sym: member}
			}
		case symbolEnum:
			return exprInfo{returns: []*ast.TypeExpr{{Name: "i32"}}}
		}
	}

	if len(obj.returns) != 1 || obj.returns[0] == nil {
		return exprInfo{}
	}

	typ := obj.returns[0]
	if method := primitiveMethodSig(typ, expr.Field); method != nil {
		return exprInfo{
			returns: []*ast.TypeExpr{fnTypeFromSig(method)},
			sym:     &symbol{kind: symbolFn, name: method.name, fn: method, line: expr.Line},
		}
	}
	class, iface := c.lookupObjectType(ctx.mod, typ.Name)
	if class != nil {
		if field, ok := class.fields[expr.Field]; ok {
			return exprInfo{returns: []*ast.TypeExpr{field}}
		}
		if method, ok := class.methods[expr.Field]; ok {
			return exprInfo{returns: []*ast.TypeExpr{fnTypeFromSig(method)}, sym: &symbol{kind: symbolFn, name: method.name, fn: method, line: method.line}}
		}
		c.addDiag(ctx.mod.path, expr.Line, 0, "%s has no member %q", typ.Name, expr.Field)
		return exprInfo{}
	}
	if iface != nil {
		if method, ok := iface.methods[expr.Field]; ok {
			return exprInfo{returns: []*ast.TypeExpr{fnTypeFromSig(method)}, sym: &symbol{kind: symbolFn, name: method.name, fn: method, line: method.line}}
		}
		c.addDiag(ctx.mod.path, expr.Line, 0, "%s has no member %q", typ.Name, expr.Field)
		return exprInfo{}
	}
	if isPrimitiveTypeName(typ.Name) {
		c.addDiag(ctx.mod.path, expr.Line, 0, "%s has no member %q", typ.Name, expr.Field)
	}

	return exprInfo{}
}

func (c *Checker) callInfo(ctx *bodyContext, expr *ast.CallExpr) exprInfo {
	callee := c.checkExpr(ctx, expr.Callee)
	argInfos := make([]exprInfo, len(expr.Args))
	for i, arg := range expr.Args {
		argInfos[i] = c.checkExpr(ctx, arg)
		if len(argInfos[i].returns) > 1 {
			c.addDiag(ctx.mod.path, expr.Line, 0, "call arguments must be single values")
		}
	}

	if callee.sym != nil {
		switch callee.sym.kind {
		case symbolFn:
			c.checkCall(ctx.mod, expr.Line, callee.sym.name, callee.sym.fn, argInfos)
			c.checkBuiltinCallSemantics(ctx, expr.Callee, argInfos)
			return exprInfo{returns: callee.sym.fn.ret}
		case symbolClass:
			if callee.sym.class.init != nil {
				c.checkCall(ctx.mod, expr.Line, callee.sym.name, callee.sym.class.init, argInfos)
			} else if len(expr.Args) != 0 {
				c.addDiag(ctx.mod.path, expr.Line, 0, "%s expects 0 arguments, got %d", callee.sym.name, len(expr.Args))
			}
			if member, ok := expr.Callee.(*ast.MemberExpr); ok {
				if base, ok := member.Object.(*ast.Ident); ok {
					if modSym, exists := ctx.mod.symbols[base.Name]; exists && modSym.kind == symbolModule {
						return exprInfo{returns: []*ast.TypeExpr{{Name: base.Name + "." + member.Field}}}
					}
				}
			}
			return exprInfo{returns: []*ast.TypeExpr{{Name: callee.sym.class.name}}}
		}
	}

	if len(callee.returns) == 1 && callee.returns[0] != nil && callee.returns[0].Name == "fn" && callee.returns[0].ParamTypes != nil {
		c.checkCall(ctx.mod, expr.Line, "function value", &funcSig{
			name:   "function value",
			params: callee.returns[0].ParamTypes,
			ret:    singleReturnType(callee.returns[0].ReturnType),
		}, argInfos)
		if callee.returns[0].ReturnType != nil {
			return exprInfo{returns: []*ast.TypeExpr{callee.returns[0].ReturnType}}
		}
	}
	if len(callee.returns) == 1 && callee.returns[0] != nil && callee.returns[0].Name == "ffi.Func" {
		return exprInfo{returns: []*ast.TypeExpr{nil}}
	}

	return exprInfo{}
}

func (c *Checker) checkBuiltinCallSemantics(ctx *bodyContext, callee ast.Expr, args []exprInfo) {
	member, ok := callee.(*ast.MemberExpr)
	if !ok {
		return
	}
	obj, ok := member.Object.(*ast.Ident)
	if !ok {
		return
	}
	modSym, ok := ctx.mod.symbols[obj.Name]
	if !ok || modSym.kind != symbolModule || modSym.module == nil || !modSym.builtin {
		return
	}

	switch modSym.module.path + "." + member.Field {
	case "<builtin:thread>.spawn":
		if len(args) == 0 || len(args[0].returns) != 1 || args[0].returns[0] == nil {
			return
		}
		fnType := args[0].returns[0]
		if fnType.Name != "fn" || fnType.ParamTypes == nil {
			return
		}
		if len(args)-1 != len(fnType.ParamTypes) {
			c.addDiag(ctx.mod.path, member.Line, 0, "thread.spawn target expects %d arguments, got %d", len(fnType.ParamTypes), len(args)-1)
			return
		}
		for i := 1; i < len(args); i++ {
			if len(args[i].returns) != 1 || args[i].returns[0] == nil {
				continue
			}
			if !c.isAssignable(ctx.mod, fnType.ParamTypes[i-1], args[i].returns[0]) {
				c.addDiag(ctx.mod.path, member.Line, 0, "thread.spawn arg %d expects %s, received %s", i, typeString(fnType.ParamTypes[i-1]), typeString(args[i].returns[0]))
			}
		}
	case "<builtin:sort>.by":
		if len(args) != 2 || len(args[0].returns) != 1 || len(args[1].returns) != 1 || args[0].returns[0] == nil || args[1].returns[0] == nil {
			return
		}
		arrType := args[0].returns[0]
		fnType := args[1].returns[0]
		if arrType.Name != "array" || arrType.ElemType == nil || fnType.Name != "fn" || fnType.ParamTypes == nil {
			return
		}
		if len(fnType.ParamTypes) != 2 {
			c.addDiag(ctx.mod.path, member.Line, 0, "sort.by comparator expects 2 arguments, got %d", len(fnType.ParamTypes))
			return
		}
		for i, paramType := range fnType.ParamTypes {
			if paramType != nil && !c.isAssignable(ctx.mod, paramType, arrType.ElemType) {
				c.addDiag(ctx.mod.path, member.Line, 0, "sort.by comparator arg %d expects %s, received %s", i+1, typeString(paramType), typeString(arrType.ElemType))
			}
		}
		if fnType.ReturnType != nil && fnType.ReturnType.Name != "bool" {
			c.addDiag(ctx.mod.path, member.Line, 0, "sort.by comparator must return bool, received %s", typeString(fnType.ReturnType))
		}
	case "<builtin:log>.set_handler":
		if len(args) != 1 || len(args[0].returns) != 1 || args[0].returns[0] == nil {
			return
		}
		fnType := args[0].returns[0]
		if fnType.Name != "fn" || fnType.ParamTypes == nil {
			return
		}
		if len(fnType.ParamTypes) != 2 {
			c.addDiag(ctx.mod.path, member.Line, 0, "log.set_handler handler expects 2 arguments, got %d", len(fnType.ParamTypes))
			return
		}
		if fnType.ParamTypes[0] != nil && fnType.ParamTypes[0].Name != "string" {
			c.addDiag(ctx.mod.path, member.Line, 0, "log.set_handler handler arg 1 expects string, received %s", typeString(fnType.ParamTypes[0]))
		}
		if fnType.ParamTypes[1] != nil && fnType.ParamTypes[1].Name != "string" {
			c.addDiag(ctx.mod.path, member.Line, 0, "log.set_handler handler arg 2 expects string, received %s", typeString(fnType.ParamTypes[1]))
		}
	case "<builtin:http>.listen":
		if len(args) != 2 || len(args[1].returns) != 1 || args[1].returns[0] == nil {
			return
		}
		fnType := args[1].returns[0]
		if fnType.Name != "fn" || fnType.ParamTypes == nil {
			return
		}
		if len(fnType.ParamTypes) != 1 {
			c.addDiag(ctx.mod.path, member.Line, 0, "http.listen handler expects 1 argument, got %d", len(fnType.ParamTypes))
			return
		}
		if fnType.ParamTypes[0] != nil && !c.isAssignable(ctx.mod, &ast.TypeExpr{Name: "HttpRequest"}, fnType.ParamTypes[0]) {
			c.addDiag(ctx.mod.path, member.Line, 0, "http.listen handler arg 1 expects HttpRequest, received %s", typeString(fnType.ParamTypes[0]))
		}
	}
}

func (c *Checker) checkCall(mod *moduleInfo, line int, name string, sig *funcSig, args []exprInfo) {
	if sig == nil {
		return
	}
	minArgs, maxArgs := sig.arityBounds()
	if len(args) < minArgs || (maxArgs >= 0 && len(args) > maxArgs) {
		c.addDiag(mod.path, line, 0, "%s %s, got %d", name, formatArityExpectation(minArgs, maxArgs), len(args))
		return
	}
	for i := range args {
		paramType := sig.paramType(i)
		if len(args[i].returns) != 1 || args[i].returns[0] == nil || paramType == nil {
			continue
		}
		if !c.isAssignable(mod, paramType, args[i].returns[0]) {
			c.addDiag(mod.path, line, 0, "%s arg %d expects %s, received %s", name, i+1, typeString(paramType), typeString(args[i].returns[0]))
		}
	}
}

func formatArityExpectation(minArgs int, maxArgs int) string {
	switch {
	case maxArgs < 0:
		return fmt.Sprintf("expects at least %d arguments", minArgs)
	case minArgs == maxArgs:
		return fmt.Sprintf("expects %d arguments", minArgs)
	case minArgs == 0:
		return fmt.Sprintf("expects at most %d arguments", maxArgs)
	default:
		return fmt.Sprintf("expects %d to %d arguments", minArgs, maxArgs)
	}
}

func (c *Checker) lookupObjectType(mod *moduleInfo, name string) (*classInfo, *interfaceInfo) {
	if name == "" {
		return nil, nil
	}

	baseMod := mod
	typeName := name
	if strings.Contains(name, ".") {
		if mod == nil {
			return nil, nil
		}
		parts := strings.SplitN(name, ".", 2)
		importSym, ok := mod.imports[parts[0]]
		if !ok || importSym.module == nil {
			return nil, nil
		}
		baseMod = importSym.module
		typeName = parts[1]
	}

	if baseMod != nil {
		if sym, ok := baseMod.symbols[typeName]; ok {
			if sym.class != nil {
				return sym.class, nil
			}
			if sym.iface != nil {
				return nil, sym.iface
			}
		}
	}

	class, iface := c.lookupBuiltinExportType(typeName)
	if class != nil || iface != nil {
		return class, iface
	}

	return nil, nil
}

func (c *Checker) lookupBuiltinExportType(name string) (*classInfo, *interfaceInfo) {
	var foundClass *classInfo
	var foundIface *interfaceInfo
	for _, mod := range c.builtin {
		sym, ok := mod.exports[name]
		if !ok {
			continue
		}
		if sym.class != nil {
			if foundClass != nil && foundClass != sym.class {
				return nil, nil
			}
			foundClass = sym.class
		}
		if sym.iface != nil {
			if foundIface != nil && foundIface != sym.iface {
				return nil, nil
			}
			foundIface = sym.iface
		}
	}
	if foundClass != nil || foundIface != nil {
		return foundClass, foundIface
	}
	return nil, nil
}

func (c *Checker) isAssignable(mod *moduleInfo, expected, actual *ast.TypeExpr) bool {
	if expected == nil || actual == nil {
		return true
	}
	if sameType(expected, actual) {
		return true
	}
	if expected.Name == "array" && actual.Name == "array" {
		if expected.ElemType == nil || actual.ElemType == nil {
			return true
		}
		return c.isAssignable(mod, expected.ElemType, actual.ElemType)
	}
	if expected.Name == "map" && actual.Name == "map" {
		keyOK := expected.KeyType == nil || actual.KeyType == nil || c.isAssignable(mod, expected.KeyType, actual.KeyType)
		valOK := expected.ValType == nil || actual.ValType == nil || c.isAssignable(mod, expected.ValType, actual.ValType)
		return keyOK && valOK
	}
	if expected.Name == "fn" && actual.Name == "fn" {
		if expected.ParamTypes == nil && expected.ReturnType == nil {
			return true
		}
		if len(expected.ParamTypes) != len(actual.ParamTypes) {
			return false
		}
		for i := range expected.ParamTypes {
			if !sameType(expected.ParamTypes[i], actual.ParamTypes[i]) {
				return false
			}
		}
		if expected.ReturnType == nil {
			return true
		}
		return sameType(expected.ReturnType, actual.ReturnType)
	}

	expectedClass, expectedIface := c.lookupObjectType(mod, expected.Name)
	actualClass, actualIface := c.lookupObjectType(mod, actual.Name)
	if expectedClass != nil && actualClass != nil && expectedClass == actualClass {
		return true
	}
	if expectedIface != nil && actualIface != nil && expectedIface == actualIface {
		return true
	}
	if expectedIface != nil && actualClass != nil {
		for _, ifaceName := range actualClass.implements {
			if ifaceName == expected.Name {
				return true
			}
		}
	}

	return false
}

func (c *Checker) resolveImport(name string) (string, error) {
	fileName := name + ".basl"
	for _, dir := range c.searchPaths {
		fullPath := filepath.Join(dir, fileName)
		absPath, err := filepath.Abs(fullPath)
		if err == nil && c.fileExists(absPath) {
			return absPath, nil
		}

		pkgPath := filepath.Join(dir, name, "lib", fileName)
		absPkg, err := filepath.Abs(pkgPath)
		if err == nil && c.fileExists(absPkg) {
			return absPkg, nil
		}
	}
	return "", fmt.Errorf("module %q not found", name)
}

func (c *Checker) readFile(path string) ([]byte, error) {
	if src, ok := c.overlays[path]; ok {
		return []byte(src), nil
	}
	return os.ReadFile(path)
}

func (c *Checker) fileExists(path string) bool {
	if _, ok := c.overlays[path]; ok {
		return true
	}
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

func normalizeOverlays(overlays map[string]string) map[string]string {
	if len(overlays) == 0 {
		return nil
	}
	out := make(map[string]string, len(overlays))
	for path, src := range overlays {
		absPath, err := filepath.Abs(path)
		if err != nil {
			continue
		}
		out[absPath] = src
	}
	return out
}

func (c *Checker) hasNameConflict(mod *moduleInfo, name string) bool {
	if _, ok := mod.symbols[name]; ok {
		return true
	}
	if _, ok := mod.imports[name]; ok {
		return true
	}
	return false
}

func fnSigFromDecl(decl *ast.FnDecl) *funcSig {
	sig := &funcSig{
		name: decl.Name,
		line: decl.Line,
	}
	for _, p := range decl.Params {
		sig.params = append(sig.params, p.Type)
	}
	sig.ret = returnTypes(decl.Return)
	return sig
}

func fnSigFromInterfaceMethod(method ast.InterfaceMethod) *funcSig {
	sig := &funcSig{
		name: method.Name,
		line: method.Line,
	}
	for _, p := range method.Params {
		sig.params = append(sig.params, p.Type)
	}
	sig.ret = returnTypes(method.Return)
	return sig
}

func returnTypes(rt *ast.ReturnType) []*ast.TypeExpr {
	if rt == nil {
		return nil
	}
	if len(rt.Types) == 1 && rt.Types[0] != nil && rt.Types[0].Name == "void" {
		return nil
	}
	return append([]*ast.TypeExpr(nil), rt.Types...)
}

func fnTypeFromSig(sig *funcSig) *ast.TypeExpr {
	if sig == nil {
		return &ast.TypeExpr{Name: "fn"}
	}
	var ret *ast.TypeExpr
	if len(sig.ret) == 1 {
		ret = sig.ret[0]
	}
	return &ast.TypeExpr{
		Name:       "fn",
		ParamTypes: append([]*ast.TypeExpr(nil), sig.params...),
		ReturnType: ret,
	}
}

func singleReturnType(t *ast.TypeExpr) []*ast.TypeExpr {
	if t == nil {
		return nil
	}
	return []*ast.TypeExpr{t}
}

func newBuiltinModule(name string) *moduleInfo {
	mod := &moduleInfo{
		path:      "<builtin:" + name + ">",
		symbols:   make(map[string]*symbol),
		imports:   make(map[string]*symbol),
		exports:   make(map[string]*symbol),
		validated: true,
		state:     stateLoaded,
	}

	addFn := func(fnName string, ret []*ast.TypeExpr, params ...*ast.TypeExpr) {
		sig := &funcSig{name: fnName, params: params, ret: ret}
		sym := &symbol{kind: symbolFn, name: fnName, fn: sig}
		mod.symbols[fnName] = sym
		mod.exports[fnName] = sym
	}
	addBoundedFn := func(fnName string, ret []*ast.TypeExpr, minArgs int, maxArgs int, params ...*ast.TypeExpr) {
		sig := &funcSig{
			name:           fnName,
			params:         params,
			ret:            ret,
			hasArityBounds: true,
			minArgs:        minArgs,
			maxArgs:        maxArgs,
		}
		sym := &symbol{kind: symbolFn, name: fnName, fn: sig}
		mod.symbols[fnName] = sym
		mod.exports[fnName] = sym
	}
	addBoundedVariadicFn := func(fnName string, ret []*ast.TypeExpr, minArgs int, params ...*ast.TypeExpr) {
		sig := &funcSig{
			name:           fnName,
			params:         params,
			ret:            ret,
			hasArityBounds: true,
			minArgs:        minArgs,
			maxArgs:        -1,
			variadic:       true,
		}
		sym := &symbol{kind: symbolFn, name: fnName, fn: sig}
		mod.symbols[fnName] = sym
		mod.exports[fnName] = sym
	}
	addConst := func(constName string, typ *ast.TypeExpr) {
		sym := &symbol{kind: symbolConst, name: constName, typ: typ}
		mod.symbols[constName] = sym
		mod.exports[constName] = sym
	}
	addClass := func(class *classInfo) {
		exportName := class.name
		if idx := strings.LastIndex(exportName, "."); idx >= 0 {
			exportName = exportName[idx+1:]
		}
		sym := &symbol{kind: symbolClass, name: exportName, class: class}
		mod.symbols[exportName] = sym
		mod.exports[exportName] = sym
	}

	typString := &ast.TypeExpr{Name: "string"}
	typI32 := &ast.TypeExpr{Name: "i32"}
	typF64 := &ast.TypeExpr{Name: "f64"}
	typI64 := &ast.TypeExpr{Name: "i64"}
	typU8 := &ast.TypeExpr{Name: "u8"}
	typU32 := &ast.TypeExpr{Name: "u32"}
	typU64 := &ast.TypeExpr{Name: "u64"}
	typBool := &ast.TypeExpr{Name: "bool"}
	typErr := &ast.TypeExpr{Name: "err"}
	typFn := &ast.TypeExpr{Name: "fn"}
	typArrayString := &ast.TypeExpr{Name: "array", ElemType: &ast.TypeExpr{Name: "string"}}
	typArrayArrayString := &ast.TypeExpr{Name: "array", ElemType: &ast.TypeExpr{Name: "array", ElemType: typString}}
	typMapStringString := &ast.TypeExpr{Name: "map", KeyType: typString, ValType: typString}
	typFileStat := &ast.TypeExpr{Name: "file.FileStat"}
	typFile := &ast.TypeExpr{Name: "file.File"}
	typFileEntry := &ast.TypeExpr{Name: "file.Entry"}
	typWalkIssue := &ast.TypeExpr{Name: "file.WalkIssue"}
	typArrayFileEntry := &ast.TypeExpr{Name: "array", ElemType: typFileEntry}
	typArrayWalkIssue := &ast.TypeExpr{Name: "array", ElemType: typWalkIssue}
	typRegex := &ast.TypeExpr{Name: "regex.Regex"}
	typArgParser := &ast.TypeExpr{Name: "args.ArgParser"}
	typArgsResult := &ast.TypeExpr{Name: "args.Result"}
	typJsonValue := &ast.TypeExpr{Name: "json.Value"}
	typXmlValue := &ast.TypeExpr{Name: "xml.Value"}
	typArrayXmlValue := &ast.TypeExpr{Name: "array", ElemType: typXmlValue}
	typHttpResponse := &ast.TypeExpr{Name: "HttpResponse"}
	typTcpConn := &ast.TypeExpr{Name: "TcpConn"}
	typTcpListener := &ast.TypeExpr{Name: "TcpListener"}
	typUdpConn := &ast.TypeExpr{Name: "UdpConn"}
	typSqliteDB := &ast.TypeExpr{Name: "SqliteDB"}
	typSqliteRows := &ast.TypeExpr{Name: "SqliteRows"}
	typThread := &ast.TypeExpr{Name: "Thread"}
	typMutex := &ast.TypeExpr{Name: "Mutex"}
	typTestT := &ast.TypeExpr{Name: "test.T"}
	typFfiLib := &ast.TypeExpr{Name: "ffi.Lib"}
	typFfiFunc := &ast.TypeExpr{Name: "ffi.Func"}
	typUnsafePtr := &ast.TypeExpr{Name: "unsafe.Ptr"}
	typUnsafeBuffer := &ast.TypeExpr{Name: "unsafe.Buffer"}
	typUnsafeLayout := &ast.TypeExpr{Name: "unsafe.Layout"}
	typUnsafeStruct := &ast.TypeExpr{Name: "unsafe.Struct"}
	typUnsafeCallback := &ast.TypeExpr{Name: "unsafe.Callback"}

	switch name {
	case "fmt":
		addFn("print", singleReturnType(typErr), nil)
		addFn("println", singleReturnType(typErr), nil)
		addFn("eprint", singleReturnType(typErr), nil)
		addFn("eprintln", singleReturnType(typErr), nil)
		addBoundedVariadicFn("sprintf", singleReturnType(typString), 1, typString, nil)
		addFn("dollar", singleReturnType(typString), nil)
	case "os":
		addFn("args", singleReturnType(typArrayString))
		addFn("env", []*ast.TypeExpr{typString, typBool}, typString)
		addFn("set_env", singleReturnType(typErr), typString, typString)
		addFn("exit", nil, typI32)
		addFn("cwd", []*ast.TypeExpr{typString, typErr})
		addFn("hostname", []*ast.TypeExpr{typString, typErr})
		addFn("platform", singleReturnType(typString))
		addFn("temp_dir", singleReturnType(typString))
		addFn("mkdir", singleReturnType(typErr), typString)
		addFn("rmdir", singleReturnType(typErr), typString)
		addBoundedVariadicFn("exec", []*ast.TypeExpr{typString, typString, typI32, typErr}, 1, typString, typString)
		addFn("system", []*ast.TypeExpr{typString, typString, typI32, typErr}, typString)
	case "path":
		addBoundedVariadicFn("join", singleReturnType(typString), 0, typString)
		addFn("dir", singleReturnType(typString), typString)
		addFn("base", singleReturnType(typString), typString)
		addFn("ext", singleReturnType(typString), typString)
		addFn("abs", []*ast.TypeExpr{typString, typErr}, typString)
	case "io":
		addFn("read_line", []*ast.TypeExpr{typString, typErr})
		addFn("input", []*ast.TypeExpr{typString, typErr}, typString)
		addFn("read_f64", []*ast.TypeExpr{typF64, typErr}, typString)
		addFn("read_i32", []*ast.TypeExpr{typI32, typErr}, typString)
		addFn("read_string", []*ast.TypeExpr{typString, typErr}, typString)
		addFn("read_all", []*ast.TypeExpr{typString, typErr})
		addFn("read", []*ast.TypeExpr{typString, typErr}, typI32)
	case "file":
		addFn("read_all", []*ast.TypeExpr{typString, typErr}, typString)
		addFn("read_lines", []*ast.TypeExpr{typArrayString, typErr}, typString)
		addFn("readlink", []*ast.TypeExpr{typString, typErr}, typString)
		addFn("list_dir", []*ast.TypeExpr{typArrayString, typErr}, typString)
		addFn("read_dir", []*ast.TypeExpr{typArrayString, typErr}, typString)
		addFn("walk", []*ast.TypeExpr{typArrayFileEntry, typErr}, typString)
		addFn("walk_follow_links", []*ast.TypeExpr{typArrayFileEntry, typErr}, typString)
		addFn("walk_best_effort", []*ast.TypeExpr{typArrayFileEntry, typArrayWalkIssue}, typString)
		addFn("walk_follow_links_best_effort", []*ast.TypeExpr{typArrayFileEntry, typArrayWalkIssue}, typString)
		addFn("exists", singleReturnType(typBool), typString)
		addFn("write_all", singleReturnType(typErr), typString, typString)
		addFn("write", singleReturnType(typErr), typString, typString)
		addFn("append", singleReturnType(typErr), typString, typString)
		addFn("remove", singleReturnType(typErr), typString)
		addFn("rename", singleReturnType(typErr), typString, typString)
		addFn("copy", singleReturnType(typErr), typString, typString)
		addFn("symlink", singleReturnType(typErr), typString, typString)
		addFn("link", singleReturnType(typErr), typString, typString)
		addFn("chmod", singleReturnType(typErr), typString, typI32)
		addFn("mkdir", singleReturnType(typErr), typString)
		addFn("touch", singleReturnType(typErr), typString)
		addFn("open", []*ast.TypeExpr{typFile, typErr}, typString, typString)
		addFn("stat", []*ast.TypeExpr{typFileStat, typErr}, typString)

		addClass(&classInfo{
			name: "file.File",
			methods: map[string]*funcSig{
				"write":     {name: "write", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typErr}},
				"read":      {name: "read", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{typString, typErr}},
				"read_line": {name: "read_line", ret: []*ast.TypeExpr{typString, typErr}},
				"close":     {name: "close", ret: []*ast.TypeExpr{typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
		addClass(&classInfo{
			name: "file.FileStat",
			fields: map[string]*ast.TypeExpr{
				"size":     typI32,
				"is_dir":   typBool,
				"mod_time": typString,
				"name":     typString,
				"mode":     typI32,
			},
			methods: make(map[string]*funcSig),
		})
		addClass(&classInfo{
			name: "file.Entry",
			fields: map[string]*ast.TypeExpr{
				"path":     typString,
				"name":     typString,
				"is_dir":   typBool,
				"size":     typI32,
				"mode":     typI32,
				"mod_time": typString,
			},
			methods: make(map[string]*funcSig),
		})
		addClass(&classInfo{
			name: "file.WalkIssue",
			fields: map[string]*ast.TypeExpr{
				"path": typString,
				"err":  typErr,
			},
			methods: make(map[string]*funcSig),
		})
	case "regex":
		addFn("compile", []*ast.TypeExpr{typRegex, typErr}, typString)
		addFn("match", []*ast.TypeExpr{typBool, typErr}, typString, typString)
		addFn("find", []*ast.TypeExpr{typString, typErr}, typString, typString)
		addFn("find_all", []*ast.TypeExpr{typArrayString, typErr}, typString, typString)
		addFn("replace", []*ast.TypeExpr{typString, typErr}, typString, typString, typString)
		addFn("split", []*ast.TypeExpr{typArrayString, typErr}, typString, typString)
		addClass(&classInfo{
			name: "regex.Regex",
			methods: map[string]*funcSig{
				"match":    {name: "match", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typBool}},
				"find":     {name: "find", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typString}},
				"find_all": {name: "find_all", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typArrayString}},
				"replace":  {name: "replace", params: []*ast.TypeExpr{typString, typString}, ret: []*ast.TypeExpr{typString}},
				"split":    {name: "split", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typArrayString}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "args":
		addFn("parser", singleReturnType(typArgParser), typString, typString)
		addClass(&classInfo{
			name: "args.ArgParser",
			methods: map[string]*funcSig{
				"flag": {
					name:           "flag",
					params:         []*ast.TypeExpr{typString, typString, typString, typString, typString},
					ret:            []*ast.TypeExpr{typErr},
					hasArityBounds: true,
					minArgs:        4,
					maxArgs:        5,
				},
				"arg": {
					name:           "arg",
					params:         []*ast.TypeExpr{typString, typString, typString, {Name: "bool"}},
					ret:            []*ast.TypeExpr{typErr},
					hasArityBounds: true,
					minArgs:        3,
					maxArgs:        4,
				},
				"parse_result": {name: "parse_result", ret: []*ast.TypeExpr{typArgsResult, typErr}},
				"parse":        {name: "parse", ret: []*ast.TypeExpr{{Name: "map", KeyType: typString, ValType: typString}, typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
		addClass(&classInfo{
			name: "args.Result",
			methods: map[string]*funcSig{
				"get_string": {name: "get_string", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typString}},
				"get_bool":   {name: "get_bool", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typBool}},
				"get_list":   {name: "get_list", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typArrayString}},
				"get_i32":    {name: "get_i32", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typI32}},
				"get_i64":    {name: "get_i64", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typI64}},
				"get_f64":    {name: "get_f64", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typF64}},
				"get_u32":    {name: "get_u32", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typU32}},
				"get_u64":    {name: "get_u64", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typU64}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "math":
		for _, fnName := range []string{"sqrt", "abs", "floor", "ceil", "round", "sin", "cos", "tan", "log"} {
			addFn(fnName, singleReturnType(typF64), typF64)
		}
		addFn("pow", singleReturnType(typF64), typF64, typF64)
		addFn("min", singleReturnType(typF64), typF64, typF64)
		addFn("max", singleReturnType(typF64), typF64, typF64)
		addFn("random", singleReturnType(typF64))
		addConst("pi", typF64)
		addConst("e", typF64)
	case "strings":
		addFn("join", singleReturnType(typString), typArrayString, typString)
		addFn("repeat", singleReturnType(typString), typString, typI32)
	case "time":
		addFn("now", singleReturnType(typI64))
		addFn("sleep", nil, typI32)
		addFn("since", singleReturnType(typI64), typI64)
		addFn("format", singleReturnType(typString), typI64, typString)
		addFn("parse", []*ast.TypeExpr{typI64, typErr}, typString, typString)
	case "log":
		addFn("debug", nil, nil)
		addFn("info", nil, nil)
		addFn("warn", nil, nil)
		addFn("error", nil, nil)
		addFn("fatal", nil, nil)
		addFn("set_level", nil, typString)
		addFn("set_handler", nil, &ast.TypeExpr{Name: "fn", ParamTypes: []*ast.TypeExpr{typString, typString}})
	case "base64":
		addFn("encode", singleReturnType(typString), typString)
		addFn("decode", []*ast.TypeExpr{typString, typErr}, typString)
	case "hex":
		addFn("encode", singleReturnType(typString), typString)
		addFn("decode", []*ast.TypeExpr{typString, typErr}, typString)
	case "hash":
		addFn("sha256", singleReturnType(typString), typString)
		addFn("sha512", singleReturnType(typString), typString)
		addFn("sha1", singleReturnType(typString), typString)
		addFn("md5", singleReturnType(typString), typString)
		addFn("hmac_sha256", singleReturnType(typString), typString, typString)
	case "mime":
		addFn("type_by_ext", singleReturnType(typString), typString)
		addFn("ext_by_type", singleReturnType(typString), typString)
	case "csv":
		addFn("parse", []*ast.TypeExpr{typArrayArrayString, typErr}, typString)
		addFn("stringify", []*ast.TypeExpr{typString, typErr}, typArrayArrayString)
	case "archive":
		addFn("tar_create", singleReturnType(typErr), typString, typArrayString)
		addFn("tar_extract", singleReturnType(typErr), typString, typString)
		addFn("zip_create", singleReturnType(typErr), typString, typArrayString)
		addFn("zip_extract", singleReturnType(typErr), typString, typString)
	case "compress":
		addFn("gzip", []*ast.TypeExpr{typString, typErr}, typString)
		addFn("gunzip", []*ast.TypeExpr{typString, typErr}, typString)
		addFn("zlib", []*ast.TypeExpr{typString, typErr}, typString)
		addFn("unzlib", []*ast.TypeExpr{typString, typErr}, typString)
	case "crypto":
		addFn("aes_encrypt", []*ast.TypeExpr{typString, typErr}, typString, typString)
		addFn("aes_decrypt", []*ast.TypeExpr{typString, typErr}, typString, typString)
		addFn("rsa_generate", []*ast.TypeExpr{typString, typString, typErr}, typI32)
		addFn("rsa_encrypt", []*ast.TypeExpr{typString, typErr}, typString, typString)
		addFn("rsa_decrypt", []*ast.TypeExpr{typString, typErr}, typString, typString)
		addFn("rsa_sign", []*ast.TypeExpr{typString, typErr}, typString, typString)
		addFn("rsa_verify", singleReturnType(typBool), typString, typString, typString)
	case "rand":
		addFn("bytes", singleReturnType(typString), typI32)
		addFn("int", singleReturnType(typI32), typI32, typI32)
	case "sort":
		addFn("ints", nil, &ast.TypeExpr{Name: "array", ElemType: typI32})
		addFn("strings", nil, typArrayString)
		addFn("by", nil, &ast.TypeExpr{Name: "array"}, typFn)
	case "json":
		addFn("parse", []*ast.TypeExpr{typJsonValue, typErr}, typString)
		addFn("stringify", singleReturnType(typString), nil)
		addClass(&classInfo{
			name: "json.Value",
			methods: map[string]*funcSig{
				"get_string": {name: "get_string", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typString}},
				"get_i32":    {name: "get_i32", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typI32}},
				"get_f64":    {name: "get_f64", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typF64}},
				"get_bool":   {name: "get_bool", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typBool}},
				"get":        {name: "get", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typJsonValue, typErr}},
				"at":         {name: "at", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{typJsonValue, typErr}},
				"at_i32":     {name: "at_i32", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{typI32}},
				"at_string":  {name: "at_string", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{typString}},
				"len":        {name: "len", ret: []*ast.TypeExpr{typI32}},
				"keys":       {name: "keys", ret: []*ast.TypeExpr{typArrayString}},
				"is_object":  {name: "is_object", ret: []*ast.TypeExpr{typBool}},
				"is_array":   {name: "is_array", ret: []*ast.TypeExpr{typBool}},
				"to_string":  {name: "to_string", ret: []*ast.TypeExpr{typString}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "xml":
		addFn("parse", []*ast.TypeExpr{typXmlValue, typErr}, typString)
		addClass(&classInfo{
			name: "xml.Value",
			methods: map[string]*funcSig{
				"tag":      {name: "tag", ret: []*ast.TypeExpr{typString}},
				"text":     {name: "text", ret: []*ast.TypeExpr{typString}},
				"attr":     {name: "attr", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typString, typBool}},
				"children": {name: "children", ret: []*ast.TypeExpr{typArrayXmlValue}},
				"find":     {name: "find", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typArrayXmlValue}},
				"find_one": {name: "find_one", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typXmlValue, typErr}},
				"len":      {name: "len", ret: []*ast.TypeExpr{typI32}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "parse":
		addFn("i32", []*ast.TypeExpr{typI32, typErr}, typString)
		addFn("i64", []*ast.TypeExpr{typI64, typErr}, typString)
		addFn("f64", []*ast.TypeExpr{typF64, typErr}, typString)
		addFn("u8", []*ast.TypeExpr{typU8, typErr}, typString)
		addFn("u32", []*ast.TypeExpr{typU32, typErr}, typString)
		addFn("u64", []*ast.TypeExpr{typU64, typErr}, typString)
		addFn("bool", []*ast.TypeExpr{typBool, typErr}, typString)
	case "http":
		addFn("get", []*ast.TypeExpr{typHttpResponse, typErr}, typString)
		addFn("post", []*ast.TypeExpr{typHttpResponse, typErr}, typString, typString)
		addBoundedFn("request", []*ast.TypeExpr{typHttpResponse, typErr}, 3, 4, typString, typString, typMapStringString, typString)
		addFn("listen", singleReturnType(typErr), typString, typFn)
		addClass(&classInfo{
			name: "HttpRequest",
			fields: map[string]*ast.TypeExpr{
				"method":  typString,
				"path":    typString,
				"query":   typString,
				"body":    typString,
				"headers": typMapStringString,
			},
			methods: make(map[string]*funcSig),
		})
		addClass(&classInfo{
			name: "HttpResponse",
			fields: map[string]*ast.TypeExpr{
				"status":  typI32,
				"body":    typString,
				"headers": typMapStringString,
			},
			methods: make(map[string]*funcSig),
		})
	case "tcp":
		addFn("connect", []*ast.TypeExpr{typTcpConn, typErr}, typString)
		addFn("listen", []*ast.TypeExpr{typTcpListener, typErr}, typString)
		addClass(&classInfo{
			name: "TcpListener",
			methods: map[string]*funcSig{
				"accept": {name: "accept", ret: []*ast.TypeExpr{typTcpConn, typErr}},
				"close":  {name: "close", ret: []*ast.TypeExpr{typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
		addClass(&classInfo{
			name: "TcpConn",
			methods: map[string]*funcSig{
				"write": {name: "write", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typErr}},
				"read":  {name: "read", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{typString, typErr}},
				"close": {name: "close", ret: []*ast.TypeExpr{typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "udp":
		addFn("send", singleReturnType(typErr), typString, typString)
		addFn("listen", []*ast.TypeExpr{typUdpConn, typErr}, typString)
		addClass(&classInfo{
			name: "UdpConn",
			methods: map[string]*funcSig{
				"recv":  {name: "recv", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{typString, typErr}},
				"close": {name: "close", ret: []*ast.TypeExpr{typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "sqlite":
		addFn("open", []*ast.TypeExpr{typSqliteDB, typErr}, typString)
		addClass(&classInfo{
			name: "SqliteDB",
			methods: map[string]*funcSig{
				"exec":  {name: "exec", params: []*ast.TypeExpr{typString, nil}, ret: []*ast.TypeExpr{typErr}, hasArityBounds: true, minArgs: 1, maxArgs: -1, variadic: true},
				"query": {name: "query", params: []*ast.TypeExpr{typString, nil}, ret: []*ast.TypeExpr{typSqliteRows, typErr}, hasArityBounds: true, minArgs: 1, maxArgs: -1, variadic: true},
				"close": {name: "close", ret: []*ast.TypeExpr{typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
		addClass(&classInfo{
			name: "SqliteRows",
			methods: map[string]*funcSig{
				"next":  {name: "next", ret: []*ast.TypeExpr{typBool}},
				"get":   {name: "get", params: []*ast.TypeExpr{typString}, ret: []*ast.TypeExpr{typString}},
				"close": {name: "close", ret: []*ast.TypeExpr{typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "thread":
		addBoundedVariadicFn("spawn", []*ast.TypeExpr{typThread, typErr}, 1, typFn, nil)
		addFn("sleep", nil, typI32)
		addClass(&classInfo{
			name: "Thread",
			methods: map[string]*funcSig{
				"join": {name: "join", ret: []*ast.TypeExpr{nil, typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "mutex":
		addFn("new", []*ast.TypeExpr{typMutex, typErr})
		addClass(&classInfo{
			name: "Mutex",
			methods: map[string]*funcSig{
				"lock":    {name: "lock", ret: []*ast.TypeExpr{typErr}},
				"unlock":  {name: "unlock", ret: []*ast.TypeExpr{typErr}},
				"destroy": {name: "destroy", ret: []*ast.TypeExpr{typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "test":
		addFn("T", singleReturnType(typTestT))
		addClass(&classInfo{
			name: "test.T",
			methods: map[string]*funcSig{
				"assert": {name: "assert", params: []*ast.TypeExpr{typBool, typString}},
				"fail":   {name: "fail", params: []*ast.TypeExpr{typString}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "ffi":
		addFn("load", []*ast.TypeExpr{typFfiLib, typErr}, typString)
		addBoundedVariadicFn("bind", []*ast.TypeExpr{typFfiFunc, typErr}, 3, typFfiLib, typString, typString, typString)
		addClass(&classInfo{
			name: "ffi.Lib",
			methods: map[string]*funcSig{
				"close": {name: "close", ret: []*ast.TypeExpr{typErr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
		addClass(&classInfo{
			name: "ffi.Func",
			methods: map[string]*funcSig{
				"call": {name: "call", params: []*ast.TypeExpr{nil}, ret: []*ast.TypeExpr{nil}, hasArityBounds: true, minArgs: 0, maxArgs: -1, variadic: true},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	case "unsafe":
		addConst("null", typUnsafePtr)
		addFn("alloc", singleReturnType(typUnsafeBuffer), typI32)
		addBoundedVariadicFn("layout", singleReturnType(typUnsafeLayout), 0, typString)
		addBoundedVariadicFn("callback", singleReturnType(typUnsafeCallback), 2, typFn, typString, typString)
		addClass(&classInfo{
			name: "unsafe.Buffer",
			methods: map[string]*funcSig{
				"len":     {name: "len", ret: []*ast.TypeExpr{typI32}},
				"get":     {name: "get", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{typU8}},
				"set":     {name: "set", params: []*ast.TypeExpr{typI32, nil}},
				"get_u32": {name: "get_u32", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{typU32}},
				"ptr":     {name: "ptr", ret: []*ast.TypeExpr{typUnsafePtr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
		addClass(&classInfo{
			name: "unsafe.Layout",
			methods: map[string]*funcSig{
				"new": {name: "new", ret: []*ast.TypeExpr{typUnsafeStruct}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
		addClass(&classInfo{
			name: "unsafe.Struct",
			methods: map[string]*funcSig{
				"get": {name: "get", params: []*ast.TypeExpr{typI32}, ret: []*ast.TypeExpr{nil}},
				"set": {name: "set", params: []*ast.TypeExpr{typI32, nil}},
				"ptr": {name: "ptr", ret: []*ast.TypeExpr{typUnsafePtr}},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
		addClass(&classInfo{
			name: "unsafe.Callback",
			methods: map[string]*funcSig{
				"ptr":  {name: "ptr", ret: []*ast.TypeExpr{typUnsafePtr}},
				"free": {name: "free"},
			},
			fields: make(map[string]*ast.TypeExpr),
		})
	}

	if len(mod.exports) > 0 {
		mod.strict = true
	}

	return mod
}

func primitiveMethodSig(typ *ast.TypeExpr, name string) *funcSig {
	if typ == nil {
		return nil
	}

	switch typ.Name {
	case "string":
		switch name {
		case "len":
			return &funcSig{name: "len", ret: []*ast.TypeExpr{{Name: "i32"}}}
		case "contains", "starts_with", "ends_with":
			return &funcSig{name: name, params: []*ast.TypeExpr{{Name: "string"}}, ret: []*ast.TypeExpr{{Name: "bool"}}}
		case "trim", "to_upper", "to_lower":
			return &funcSig{name: name, ret: []*ast.TypeExpr{{Name: "string"}}}
		case "replace":
			return &funcSig{name: "replace", params: []*ast.TypeExpr{{Name: "string"}, {Name: "string"}}, ret: []*ast.TypeExpr{{Name: "string"}}}
		case "split":
			return &funcSig{name: "split", params: []*ast.TypeExpr{{Name: "string"}}, ret: []*ast.TypeExpr{{Name: "array", ElemType: &ast.TypeExpr{Name: "string"}}}}
		case "index_of":
			return &funcSig{name: "index_of", params: []*ast.TypeExpr{{Name: "string"}}, ret: []*ast.TypeExpr{{Name: "i32"}, {Name: "bool"}}}
		case "substr":
			return &funcSig{name: "substr", params: []*ast.TypeExpr{{Name: "i32"}, {Name: "i32"}}, ret: []*ast.TypeExpr{{Name: "string"}, {Name: "err"}}}
		case "bytes":
			return &funcSig{name: "bytes", ret: []*ast.TypeExpr{{Name: "array", ElemType: &ast.TypeExpr{Name: "u8"}}}}
		case "char_at":
			return &funcSig{name: "char_at", params: []*ast.TypeExpr{{Name: "i32"}}, ret: []*ast.TypeExpr{{Name: "string"}, {Name: "err"}}}
		}
	case "array":
		elem := typ.ElemType
		switch name {
		case "len":
			return &funcSig{name: "len", ret: []*ast.TypeExpr{{Name: "i32"}}}
		case "push":
			return &funcSig{name: "push", params: []*ast.TypeExpr{elem}, ret: nil}
		case "pop":
			return &funcSig{name: "pop", ret: []*ast.TypeExpr{elem, &ast.TypeExpr{Name: "err"}}}
		case "get":
			return &funcSig{name: "get", params: []*ast.TypeExpr{{Name: "i32"}}, ret: []*ast.TypeExpr{elem, &ast.TypeExpr{Name: "err"}}}
		case "set":
			return &funcSig{name: "set", params: []*ast.TypeExpr{{Name: "i32"}, elem}, ret: []*ast.TypeExpr{{Name: "err"}}}
		case "slice":
			return &funcSig{name: "slice", params: []*ast.TypeExpr{{Name: "i32"}, {Name: "i32"}}, ret: []*ast.TypeExpr{{Name: "array", ElemType: elem}}}
		case "contains":
			return &funcSig{name: "contains", params: []*ast.TypeExpr{elem}, ret: []*ast.TypeExpr{{Name: "bool"}}}
		}
	case "map":
		key := typ.KeyType
		val := typ.ValType
		switch name {
		case "len":
			return &funcSig{name: "len", ret: []*ast.TypeExpr{{Name: "i32"}}}
		case "get":
			return &funcSig{name: "get", params: []*ast.TypeExpr{key}, ret: []*ast.TypeExpr{val, &ast.TypeExpr{Name: "bool"}}}
		case "set":
			return &funcSig{name: "set", params: []*ast.TypeExpr{key, val}, ret: []*ast.TypeExpr{{Name: "err"}}}
		case "remove":
			return &funcSig{name: "remove", params: []*ast.TypeExpr{key}, ret: []*ast.TypeExpr{val, &ast.TypeExpr{Name: "bool"}}}
		case "has":
			return &funcSig{name: "has", params: []*ast.TypeExpr{key}, ret: []*ast.TypeExpr{{Name: "bool"}}}
		case "keys":
			return &funcSig{name: "keys", ret: []*ast.TypeExpr{{Name: "array", ElemType: key}}}
		case "values":
			return &funcSig{name: "values", ret: []*ast.TypeExpr{{Name: "array", ElemType: val}}}
		}
	case "err":
		switch name {
		case "message", "kind":
			return &funcSig{name: name, ret: []*ast.TypeExpr{{Name: "string"}}}
		}
	}

	return nil
}

func isSupportedTypeConversion(target string, source string) bool {
	switch target {
	case "string":
		return true
	case "i32":
		switch source {
		case "i32", "i64", "f64", "u32", "u8", "unsafe.Ptr", "ptr":
			return true
		}
	case "i64":
		switch source {
		case "i32", "i64":
			return true
		}
	case "f64":
		switch source {
		case "i32", "i64", "f64":
			return true
		}
	case "u8":
		switch source {
		case "i32", "u8", "u32", "u64":
			return true
		}
	case "u32":
		switch source {
		case "i32", "u8", "u32", "u64":
			return true
		}
	case "u64":
		switch source {
		case "i32", "u8", "u32", "u64":
			return true
		}
	}
	return false
}

func parseFuncForTypeName(target string) string {
	switch target {
	case "i32", "i64", "f64", "u8", "u32", "u64", "bool":
		return "parse." + target
	default:
		return ""
	}
}

func isDirectCallableType(types []*ast.TypeExpr) bool {
	return len(types) == 1 && types[0] != nil && types[0].Name == "fn"
}

func isPrimitiveTypeName(name string) bool {
	switch name {
	case "string", "bool", "i32", "i64", "u8", "u32", "u64", "f32", "f64", "array", "map", "err":
		return true
	}
	return false
}

func isNumericTypeName(name string) bool {
	switch name {
	case "i32", "i64", "u8", "u32", "u64", "f32", "f64":
		return true
	}
	return false
}

func isErrKind(name string) bool {
	switch name {
	case "not_found", "permission", "exists", "eof", "io", "parse", "bounds", "type", "arg", "timeout", "closed", "state":
		return true
	}
	return false
}

func sameType(a, b *ast.TypeExpr) bool {
	if a == nil || b == nil {
		return a == b
	}
	if a.Name != b.Name {
		return false
	}
	if !sameType(a.ElemType, b.ElemType) || !sameType(a.KeyType, b.KeyType) || !sameType(a.ValType, b.ValType) || !sameType(a.ReturnType, b.ReturnType) {
		return false
	}
	if len(a.ParamTypes) != len(b.ParamTypes) {
		return false
	}
	for i := range a.ParamTypes {
		if !sameType(a.ParamTypes[i], b.ParamTypes[i]) {
			return false
		}
	}
	return true
}

func assignable(expected, actual *ast.TypeExpr) bool {
	if expected == nil || actual == nil {
		return true
	}
	if sameType(expected, actual) {
		return true
	}
	if expected.Name == "fn" && actual.Name == "fn" {
		if expected.ParamTypes == nil && expected.ReturnType == nil {
			return true
		}
		if len(expected.ParamTypes) != len(actual.ParamTypes) {
			return false
		}
		for i := range expected.ParamTypes {
			if !sameType(expected.ParamTypes[i], actual.ParamTypes[i]) {
				return false
			}
		}
		if expected.ReturnType == nil {
			return true
		}
		return sameType(expected.ReturnType, actual.ReturnType)
	}
	return false
}

func typeString(t *ast.TypeExpr) string {
	if t == nil {
		return "unknown"
	}
	switch t.Name {
	case "array":
		return "array<" + typeString(t.ElemType) + ">"
	case "map":
		return "map<" + typeString(t.KeyType) + ", " + typeString(t.ValType) + ">"
	case "fn":
		if t.ParamTypes == nil && t.ReturnType == nil {
			return "fn"
		}
		var parts []string
		for _, p := range t.ParamTypes {
			parts = append(parts, typeString(p))
		}
		if t.ReturnType == nil {
			return "fn(" + strings.Join(parts, ", ") + ")"
		}
		return "fn(" + strings.Join(parts, ", ") + ") -> " + typeString(t.ReturnType)
	default:
		return t.Name
	}
}

func shortPaths(paths []string) []string {
	out := make([]string, 0, len(paths))
	for _, p := range paths {
		out = append(out, filepath.Base(p))
	}
	return out
}
