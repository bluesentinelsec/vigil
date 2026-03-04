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
	// variadic means the final parameter type repeats for any remaining args.
	variadic bool
	line     int
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
}

func CheckFile(path string, searchPaths []string) ([]Diagnostic, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}

	c := &Checker{
		searchPaths: append([]string(nil), searchPaths...),
		builtin:     make(map[string]*moduleInfo),
		modules:     make(map[string]*moduleInfo),
	}
	for _, name := range interp.BuiltinModuleNames() {
		c.builtin[name] = newBuiltinModule(name)
	}

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

	src, err := os.ReadFile(absPath)
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
		info := c.checkExpr(ctx, s.Value)
		if len(info.returns) == 0 {
			return
		}
		if len(info.returns) != len(s.Bindings) {
			c.addDiag(ctx.mod.path, s.Line, 0, "tuple binding expects %d values, got %d", len(s.Bindings), len(info.returns))
			return
		}
		for i, binding := range s.Bindings {
			if binding.Discard {
				continue
			}
			if info.returns[i] != nil && !c.isAssignable(ctx.mod, binding.Type, info.returns[i]) {
				c.addDiag(ctx.mod.path, s.Line, 0, "tuple binding %s expects %s, received %s", binding.Name, typeString(binding.Type), typeString(info.returns[i]))
			}
			ctx.scope.define(binding.Name, binding.Type)
		}
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
			}
		}
		c.checkBlock(loopScope, s.Body)
	case *ast.SwitchStmt:
		c.checkExpr(ctx, s.Tag)
		for _, cs := range s.Cases {
			for _, val := range cs.Values {
				c.checkExpr(ctx, val)
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
		if len(arg.returns) == 1 && arg.returns[0] != nil && isStringNumericParseTarget(e.Target.Name) && arg.returns[0].Name == "string" {
			return exprInfo{returns: []*ast.TypeExpr{e.Target, {Name: "err"}}}
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
			return exprInfo{returns: []*ast.TypeExpr{{Name: "bool"}}}
		case "-":
			return exprInfo{returns: operand.returns}
		default:
			return exprInfo{returns: operand.returns}
		}
	case *ast.BinaryExpr:
		left := c.checkExpr(ctx, e.Left)
		right := c.checkExpr(ctx, e.Right)
		switch e.Op {
		case "==", "!=", "<", ">", "<=", ">=", "&&", "||":
			return exprInfo{returns: []*ast.TypeExpr{{Name: "bool"}}}
		case "+":
			if len(left.returns) == 1 && len(right.returns) == 1 && left.returns[0] != nil && right.returns[0] != nil {
				if left.returns[0].Name == "string" && right.returns[0].Name == "string" {
					return exprInfo{returns: []*ast.TypeExpr{{Name: "string"}}}
				}
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
		c.checkExpr(ctx, e.Index)
		if len(objInfo.returns) == 1 && objInfo.returns[0] != nil {
			switch objInfo.returns[0].Name {
			case "array":
				return exprInfo{returns: []*ast.TypeExpr{objInfo.returns[0].ElemType}}
			case "map":
				return exprInfo{returns: []*ast.TypeExpr{objInfo.returns[0].ValType}}
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
				if !obj.sym.builtin {
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
			return exprInfo{returns: callee.sym.fn.ret}
		case symbolClass:
			if callee.sym.class.init != nil {
				c.checkCall(ctx.mod, expr.Line, callee.sym.name, callee.sym.class.init, argInfos)
			} else if len(expr.Args) != 0 {
				c.addDiag(ctx.mod.path, expr.Line, 0, "%s expects 0 arguments, got %d", callee.sym.name, len(expr.Args))
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

	return exprInfo{}
}

func (c *Checker) checkCall(mod *moduleInfo, line int, name string, sig *funcSig, args []exprInfo) {
	if sig == nil {
		return
	}
	if sig.variadic {
		min := 0
		if len(sig.params) > 0 {
			min = len(sig.params) - 1
		}
		if len(args) < min {
			c.addDiag(mod.path, line, 0, "%s expects at least %d arguments, got %d", name, min, len(args))
			return
		}
		for i := range args {
			paramIdx := i
			if len(sig.params) > 0 && paramIdx >= len(sig.params)-1 {
				paramIdx = len(sig.params) - 1
			}
			if len(sig.params) == 0 {
				continue
			}
			if len(args[i].returns) != 1 || args[i].returns[0] == nil || sig.params[paramIdx] == nil {
				continue
			}
			if !c.isAssignable(mod, sig.params[paramIdx], args[i].returns[0]) {
				c.addDiag(mod.path, line, 0, "%s arg %d expects %s, received %s", name, i+1, typeString(sig.params[paramIdx]), typeString(args[i].returns[0]))
			}
		}
		return
	}
	if len(args) != len(sig.params) {
		c.addDiag(mod.path, line, 0, "%s expects %d arguments, got %d", name, len(sig.params), len(args))
		return
	}
	for i := range args {
		if len(args[i].returns) != 1 || args[i].returns[0] == nil || sig.params[i] == nil {
			continue
		}
		if !c.isAssignable(mod, sig.params[i], args[i].returns[0]) {
			c.addDiag(mod.path, line, 0, "%s arg %d expects %s, received %s", name, i+1, typeString(sig.params[i]), typeString(args[i].returns[0]))
		}
	}
}

func (c *Checker) lookupObjectType(mod *moduleInfo, name string) (*classInfo, *interfaceInfo) {
	if mod == nil || name == "" {
		return nil, nil
	}

	baseMod := mod
	typeName := name
	if strings.Contains(name, ".") {
		parts := strings.SplitN(name, ".", 2)
		importSym, ok := mod.imports[parts[0]]
		if !ok || importSym.module == nil {
			return nil, nil
		}
		baseMod = importSym.module
		typeName = parts[1]
	}

	sym, ok := baseMod.symbols[typeName]
	if !ok {
		return nil, nil
	}
	if sym.class != nil {
		return sym.class, nil
	}
	if sym.iface != nil {
		return nil, sym.iface
	}
	return nil, nil
}

func (c *Checker) resolveImport(name string) (string, error) {
	fileName := name + ".basl"
	for _, dir := range c.searchPaths {
		fullPath := filepath.Join(dir, fileName)
		absPath, err := filepath.Abs(fullPath)
		if err == nil {
			if info, statErr := os.Stat(absPath); statErr == nil && !info.IsDir() {
				return absPath, nil
			}
		}

		pkgPath := filepath.Join(dir, name, "lib", fileName)
		absPkg, err := filepath.Abs(pkgPath)
		if err == nil {
			if info, statErr := os.Stat(absPkg); statErr == nil && !info.IsDir() {
				return absPkg, nil
			}
		}
	}
	return "", fmt.Errorf("module %q not found", name)
}

func (c *Checker) isAssignable(mod *moduleInfo, expected, actual *ast.TypeExpr) bool {
	if assignable(expected, actual) {
		return true
	}
	if expected == nil || actual == nil {
		return true
	}

	_, expectedIface := c.lookupObjectType(mod, expected.Name)
	actualClass, _ := c.lookupObjectType(mod, actual.Name)
	if expectedIface != nil && actualClass != nil {
		for _, ifaceName := range actualClass.implements {
			if ifaceName == expected.Name {
				return true
			}
		}
	}

	return false
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
	addVariadicFn := func(fnName string, ret []*ast.TypeExpr, params ...*ast.TypeExpr) {
		sig := &funcSig{name: fnName, params: params, ret: ret, variadic: true}
		sym := &symbol{kind: symbolFn, name: fnName, fn: sig}
		mod.symbols[fnName] = sym
		mod.exports[fnName] = sym
	}
	addClass := func(class *classInfo) {
		sym := &symbol{kind: symbolClass, name: class.name, class: class}
		mod.symbols[class.name] = sym
		mod.exports[class.name] = sym
	}

	typString := &ast.TypeExpr{Name: "string"}
	typI32 := &ast.TypeExpr{Name: "i32"}
	typF64 := &ast.TypeExpr{Name: "f64"}
	typBool := &ast.TypeExpr{Name: "bool"}
	typErr := &ast.TypeExpr{Name: "err"}
	typArrayString := &ast.TypeExpr{Name: "array", ElemType: &ast.TypeExpr{Name: "string"}}
	typFileStat := &ast.TypeExpr{Name: "file.FileStat"}
	typFile := &ast.TypeExpr{Name: "file.File"}

	switch name {
	case "fmt":
		addFn("print", singleReturnType(typErr), nil)
		addFn("println", singleReturnType(typErr), nil)
		addFn("eprint", singleReturnType(typErr), nil)
		addFn("eprintln", singleReturnType(typErr), nil)
		addVariadicFn("sprintf", singleReturnType(typString), typString, nil)
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
		addVariadicFn("exec", []*ast.TypeExpr{typString, typString, typI32, typErr}, typString, typString)
		addFn("system", []*ast.TypeExpr{typString, typString, typI32, typErr}, typString)
	case "path":
		addVariadicFn("join", singleReturnType(typString), typString)
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
		addFn("read_lines", []*ast.TypeExpr{{Name: "array"}, typErr}, typString)
		addFn("readlink", []*ast.TypeExpr{typString, typErr}, typString)
		addFn("list_dir", []*ast.TypeExpr{typArrayString, typErr}, typString)
		addFn("read_dir", []*ast.TypeExpr{typArrayString, typErr}, typString)
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

func isStringNumericParseTarget(name string) bool {
	switch name {
	case "i32", "i64", "f64":
		return true
	}
	return false
}

func isDirectCallableType(types []*ast.TypeExpr) bool {
	return len(types) == 1 && types[0] != nil && types[0].Name == "fn"
}

func isPrimitiveTypeName(name string) bool {
	switch name {
	case "string", "array", "map", "err":
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
