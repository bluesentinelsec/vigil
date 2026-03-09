package editor

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	editorassets "github.com/bluesentinelsec/basl/editors"
	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/checker"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

type Position struct {
	Line int `json:"line"`
	Col  int `json:"col"`
}

type Location struct {
	Path   string `json:"path"`
	Line   int    `json:"line"`
	Col    int    `json:"col"`
	EndCol int    `json:"end_col"`
}

type Hover struct {
	Contents string    `json:"contents"`
	Location *Location `json:"location,omitempty"`
}

type CompletionItem struct {
	Label         string `json:"label"`
	Kind          string `json:"kind"`
	Detail        string `json:"detail,omitempty"`
	Documentation string `json:"documentation,omitempty"`
}

type RenameEdit struct {
	Path    string `json:"path"`
	Line    int    `json:"line"`
	Col     int    `json:"col"`
	EndCol  int    `json:"end_col"`
	NewText string `json:"new_text"`
}

type SymbolItem struct {
	Name     string       `json:"name"`
	Detail   string       `json:"detail,omitempty"`
	Kind     string       `json:"kind"`
	Location Location     `json:"location"`
	Children []SymbolItem `json:"children,omitempty"`
}

type Options struct {
	SearchPaths []string
	Overlays    map[string]string
}

type symbolKind string

const (
	kindImport    symbolKind = "import"
	kindModule    symbolKind = "module"
	kindFunction  symbolKind = "function"
	kindVariable  symbolKind = "variable"
	kindConstant  symbolKind = "constant"
	kindParameter symbolKind = "parameter"
	kindClass     symbolKind = "class"
	kindField     symbolKind = "field"
	kindMethod    symbolKind = "method"
	kindEnum      symbolKind = "enum"
	kindIface     symbolKind = "interface"
)

type typeKind string

const (
	typeUnknown   typeKind = "unknown"
	typePrimitive typeKind = "primitive"
	typeClassCtor typeKind = "class_ctor"
	typeClassInst typeKind = "class_instance"
	typeModule    typeKind = "module"
	typeCallable  typeKind = "callable"
)

type typeInfo struct {
	kind     typeKind
	name     string
	class    *classInfo
	module   *fileModel
	builtin  string
	callable *callableInfo
}

type callableInfo struct {
	params []string
	ret    []typeInfo
}

type signatureInfo struct {
	label         string
	documentation string
	params        []builtinParam
}

type symbolDef struct {
	id         string
	kind       symbolKind
	name       string
	detail     string
	doc        string
	signature  *signatureInfo
	location   *Location
	typ        typeInfo
	renameable bool
	ownerFile  *fileModel
	explicit   bool
}

type classInfo struct {
	symbol  *symbolDef
	fields  map[string]*symbolDef
	methods map[string]*symbolDef
}

type occurrence struct {
	symbol   *symbolDef
	location Location
	isDecl   bool
}

type scopeBinding struct {
	symbol *symbolDef
	typ    typeInfo
}

type scope struct {
	names  map[string]scopeBinding
	parent *scope
}

func newScope(parent *scope) *scope {
	return &scope{
		names:  make(map[string]scopeBinding),
		parent: parent,
	}
}

func (s *scope) define(name string, binding scopeBinding) {
	s.names[name] = binding
}

func (s *scope) lookup(name string) (scopeBinding, bool) {
	for cur := s; cur != nil; cur = cur.parent {
		if binding, ok := cur.names[name]; ok {
			return binding, true
		}
	}
	return scopeBinding{}, false
}

type fileModel struct {
	path          string
	src           string
	tokens        []lexer.Token
	prog          *ast.Program
	analyzer      *Analyzer
	imports       map[string]*symbolDef
	importTargets map[string]*fileModel
	classes       map[string]*classInfo
	topLevel      map[string]*symbolDef
	occurrences   []occurrence
	tokenPos      int
	analyzed      bool
}

type exprTokenCursor struct {
	path   string
	tokens []lexer.Token
	pos    int
}

type Analyzer struct {
	entry              string
	searchPaths        []string
	files              map[string]*fileModel
	builtinCompletions map[string][]string
	builtinDocs        *builtinMetadata
	overlays           map[string]string
}

func Diagnostics(path string, extraSearchPaths []string) ([]checker.Diagnostic, error) {
	return DiagnosticsWithOptions(path, Options{SearchPaths: extraSearchPaths})
}

func DiagnosticsWithOptions(path string, opts Options) ([]checker.Diagnostic, error) {
	searchPaths, err := resolveSemanticSearchPaths(path, opts.SearchPaths)
	if err != nil {
		return nil, err
	}
	return checker.CheckFileWithOverlays(path, searchPaths, opts.Overlays)
}

func Definition(path string, pos Position, extraSearchPaths []string) (*Location, error) {
	return DefinitionWithOptions(path, pos, Options{SearchPaths: extraSearchPaths})
}

func DefinitionWithOptions(path string, pos Position, opts Options) (*Location, error) {
	_, file, err := newAnalyzerWithOptions(path, opts)
	if err != nil {
		return nil, err
	}
	occ := file.occurrenceAt(pos)
	if occ == nil || occ.symbol == nil || occ.symbol.location == nil {
		return nil, nil
	}
	return occ.symbol.location, nil
}

func HoverAt(path string, pos Position, extraSearchPaths []string) (*Hover, error) {
	return HoverAtWithOptions(path, pos, Options{SearchPaths: extraSearchPaths})
}

func HoverAtWithOptions(path string, pos Position, opts Options) (*Hover, error) {
	_, file, err := newAnalyzerWithOptions(path, opts)
	if err != nil {
		return nil, err
	}
	occ := file.occurrenceAt(pos)
	if occ == nil || occ.symbol == nil {
		return nil, nil
	}
	text := occ.symbol.detail
	if occ.symbol.doc != "" {
		text += "\n\n" + occ.symbol.doc
	}
	return &Hover{
		Contents: text,
		Location: occ.symbol.location,
	}, nil
}

func References(path string, pos Position, extraSearchPaths []string) ([]Location, error) {
	return ReferencesWithOptions(path, pos, Options{SearchPaths: extraSearchPaths})
}

func ReferencesWithOptions(path string, pos Position, opts Options) ([]Location, error) {
	a, file, err := newAnalyzerWithOptions(path, opts)
	if err != nil {
		return nil, err
	}
	occ := file.occurrenceAt(pos)
	if occ == nil || occ.symbol == nil {
		return nil, nil
	}
	var out []Location
	for _, indexed := range a.files {
		for _, item := range indexed.occurrences {
			if item.symbol != nil && item.symbol.id == occ.symbol.id {
				out = append(out, item.location)
			}
		}
	}
	return dedupeLocations(out), nil
}

func Rename(path string, pos Position, newName string, extraSearchPaths []string) ([]RenameEdit, error) {
	return RenameWithOptions(path, pos, newName, Options{SearchPaths: extraSearchPaths})
}

func RenameWithOptions(path string, pos Position, newName string, opts Options) ([]RenameEdit, error) {
	a, file, err := newAnalyzerWithOptions(path, opts)
	if err != nil {
		return nil, err
	}
	occ := file.occurrenceAt(pos)
	if occ == nil || occ.symbol == nil {
		return nil, nil
	}
	if !occ.symbol.renameable {
		return nil, fmt.Errorf("symbol %q cannot be renamed", occ.symbol.name)
	}
	if !validIdentifier(newName) {
		return nil, fmt.Errorf("%q is not a valid identifier", newName)
	}
	var edits []RenameEdit
	for _, indexed := range a.files {
		for _, item := range indexed.occurrences {
			if item.symbol != nil && item.symbol.id == occ.symbol.id {
				edits = append(edits, RenameEdit{
					Path:    item.location.Path,
					Line:    item.location.Line,
					Col:     item.location.Col,
					EndCol:  item.location.EndCol,
					NewText: newName,
				})
			}
		}
	}
	return dedupeEdits(edits), nil
}

func Completions(path string, pos Position, extraSearchPaths []string) ([]CompletionItem, error) {
	return CompletionsWithOptions(path, pos, Options{SearchPaths: extraSearchPaths})
}

func CompletionsWithOptions(path string, pos Position, opts Options) ([]CompletionItem, error) {
	a, file, err := newAnalyzerWithOptions(path, opts)
	if err != nil {
		return nil, err
	}
	linePrefix := prefixAt(file.src, pos)
	memberMatch := regexp.MustCompile(`([A-Za-z_][A-Za-z0-9_]*)\.\s*$`).FindStringSubmatch(linePrefix)
	if len(memberMatch) == 2 {
		return file.memberCompletions(memberMatch[1])
	}

	items := make(map[string]CompletionItem)
	for name, sym := range file.topLevel {
		items[name] = CompletionItem{Label: name, Kind: string(sym.kind), Detail: sym.detail, Documentation: sym.doc}
	}
	for name, sym := range file.imports {
		items[name] = CompletionItem{Label: name, Kind: string(sym.kind), Detail: sym.detail, Documentation: sym.doc}
	}
	for name := range a.builtinCompletions {
		if _, ok := items[name]; !ok {
			doc := ""
			if mod, ok := a.builtinModule(name); ok {
				doc = mod.Summary
			}
			items[name] = CompletionItem{Label: name, Kind: string(kindModule), Detail: "builtin module", Documentation: doc}
		}
	}

	var out []CompletionItem
	for _, item := range items {
		out = append(out, item)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Label < out[j].Label })
	return out, nil
}

func DocumentSymbols(path string, extraSearchPaths []string) ([]SymbolItem, error) {
	return DocumentSymbolsWithOptions(path, Options{SearchPaths: extraSearchPaths})
}

func DocumentSymbolsWithOptions(path string, opts Options) ([]SymbolItem, error) {
	_, file, err := newAnalyzerWithOptions(path, opts)
	if err != nil {
		return nil, err
	}
	var out []SymbolItem
	for _, decl := range file.prog.Decls {
		switch d := decl.(type) {
		case *ast.FnDecl:
			if sym := file.topLevel[d.Name]; sym != nil && sym.location != nil {
				out = append(out, SymbolItem{
					Name:     d.Name,
					Detail:   sym.detail,
					Kind:     string(sym.kind),
					Location: *sym.location,
				})
			}
		case *ast.VarDecl:
			if sym := file.topLevel[d.Name]; sym != nil && sym.location != nil {
				out = append(out, SymbolItem{
					Name:     d.Name,
					Detail:   sym.detail,
					Kind:     string(sym.kind),
					Location: *sym.location,
				})
			}
		case *ast.ConstDecl:
			if sym := file.topLevel[d.Name]; sym != nil && sym.location != nil {
				out = append(out, SymbolItem{
					Name:     d.Name,
					Detail:   sym.detail,
					Kind:     string(sym.kind),
					Location: *sym.location,
				})
			}
		case *ast.EnumDecl:
			if sym := file.topLevel[d.Name]; sym != nil && sym.location != nil {
				out = append(out, SymbolItem{
					Name:     d.Name,
					Detail:   sym.detail,
					Kind:     string(sym.kind),
					Location: *sym.location,
				})
			}
		case *ast.InterfaceDecl:
			if sym := file.topLevel[d.Name]; sym != nil && sym.location != nil {
				out = append(out, SymbolItem{
					Name:     d.Name,
					Detail:   sym.detail,
					Kind:     string(sym.kind),
					Location: *sym.location,
				})
			}
		case *ast.ClassDecl:
			class := file.classes[d.Name]
			if class == nil || class.symbol == nil || class.symbol.location == nil {
				continue
			}
			item := SymbolItem{
				Name:     d.Name,
				Detail:   class.symbol.detail,
				Kind:     string(class.symbol.kind),
				Location: *class.symbol.location,
			}
			for _, field := range d.Fields {
				if sym := class.fields[field.Name]; sym != nil && sym.location != nil {
					item.Children = append(item.Children, SymbolItem{
						Name:     field.Name,
						Detail:   sym.detail,
						Kind:     string(sym.kind),
						Location: *sym.location,
					})
				}
			}
			for _, method := range d.Methods {
				if sym := class.methods[method.Name]; sym != nil && sym.location != nil {
					item.Children = append(item.Children, SymbolItem{
						Name:     method.Name,
						Detail:   sym.detail,
						Kind:     string(sym.kind),
						Location: *sym.location,
					})
				}
			}
			out = append(out, item)
		}
	}
	return out, nil
}

func newAnalyzer(path string, extraSearchPaths []string) (*Analyzer, *fileModel, error) {
	return newAnalyzerWithOptions(path, Options{SearchPaths: extraSearchPaths})
}

func newAnalyzerWithOptions(path string, opts Options) (*Analyzer, *fileModel, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, nil, err
	}
	searchPaths, err := resolveSemanticSearchPaths(absPath, opts.SearchPaths)
	if err != nil {
		return nil, nil, err
	}
	builtin, err := loadBuiltinCompletions()
	if err != nil {
		return nil, nil, err
	}
	builtinDocs, err := loadBuiltinMetadata()
	if err != nil {
		return nil, nil, err
	}
	a := &Analyzer{
		entry:              absPath,
		searchPaths:        searchPaths,
		files:              make(map[string]*fileModel),
		builtinCompletions: builtin,
		builtinDocs:        builtinDocs,
		overlays:           normalizeOverlays(opts.Overlays),
	}
	file, err := a.loadFile(absPath)
	if err != nil {
		return nil, nil, err
	}
	a.preloadWorkspaceFiles(absPath)
	return a, file, nil
}

func (a *Analyzer) loadFile(path string) (*fileModel, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}
	if file, ok := a.files[absPath]; ok {
		return file, nil
	}

	src, err := a.readSource(absPath)
	if err != nil {
		return nil, err
	}
	tokens, err := lexer.New(src).Tokenize()
	if err != nil {
		return nil, err
	}
	prog, err := parser.New(tokens).Parse()
	if err != nil {
		return nil, err
	}

	file := &fileModel{
		path:          absPath,
		src:           src,
		tokens:        tokens,
		prog:          prog,
		analyzer:      a,
		imports:       make(map[string]*symbolDef),
		importTargets: make(map[string]*fileModel),
		classes:       make(map[string]*classInfo),
		topLevel:      make(map[string]*symbolDef),
	}
	a.files[absPath] = file
	file.collectImports()
	file.collectTopLevel()
	file.analyze()
	return file, nil
}

func (a *Analyzer) readSource(path string) (string, error) {
	if src, ok := a.overlays[path]; ok {
		return src, nil
	}
	srcBytes, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return string(srcBytes), nil
}

func (a *Analyzer) preloadWorkspaceFiles(entryPath string) {
	for _, candidate := range workspaceFileCandidates(entryPath, a.searchPaths) {
		_, _ = a.loadFile(candidate)
	}
}

func (f *fileModel) collectImports() {
	for _, decl := range f.prog.Decls {
		imp, ok := decl.(*ast.ImportDecl)
		if !ok {
			continue
		}
		alias := imp.Alias
		explicit := alias != ""
		if alias == "" {
			parts := strings.Split(imp.Path, "/")
			alias = parts[len(parts)-1]
		}
		sym := &symbolDef{
			id:         fmt.Sprintf("%s:import:%d:%s", f.path, imp.Line, alias),
			kind:       kindImport,
			name:       alias,
			detail:     fmt.Sprintf("import %s -> %s", alias, imp.Path),
			renameable: explicit,
			ownerFile:  f,
			explicit:   explicit,
		}
		if explicit {
			sym.location = f.nextIdentLocation(imp.Alias, imp.Line)
		}
		if target, ok := f.analyzer.builtinCompletions[imp.Path]; ok {
			_ = target
			sym.typ = typeInfo{kind: typeModule, builtin: imp.Path}
			if mod, ok := f.analyzer.builtinModule(imp.Path); ok {
				sym.doc = mod.Summary
			}
			f.imports[alias] = sym
			continue
		}
		resolved, err := resolveImportPath(imp.Path, f.analyzer.searchPaths)
		if err != nil {
			f.imports[alias] = sym
			continue
		}
		target, err := f.analyzer.loadFile(resolved)
		if err != nil {
			f.imports[alias] = sym
			continue
		}
		sym.typ = typeInfo{kind: typeModule, module: target, name: imp.Path}
		sym.doc = fmt.Sprintf("module %s", imp.Path)
		if target.path != "" {
			sym.location = &Location{Path: target.path, Line: 1, Col: 1, EndCol: 1}
		}
		f.imports[alias] = sym
		f.importTargets[alias] = target
	}
}

func (f *fileModel) collectTopLevel() {
	for _, decl := range f.prog.Decls {
		switch d := decl.(type) {
		case *ast.FnDecl:
			sym := &symbolDef{
				id:         f.symbolID(d.Name, d.Line),
				kind:       kindFunction,
				name:       d.Name,
				detail:     fnDetail("fn", d.Name, d.Params, d.Return),
				doc:        fmt.Sprintf("defined in %s", filepath.Base(f.path)),
				signature:  signatureFromDecl("fn", d.Name, d.Params, d.Return, fmt.Sprintf("defined in %s", filepath.Base(f.path))),
				location:   f.nextIdentLocation(d.Name, d.Line),
				typ:        callableTypeFromDecl(d),
				renameable: true,
				ownerFile:  f,
			}
			f.topLevel[d.Name] = sym
		case *ast.VarDecl:
			sym := &symbolDef{
				id:         f.symbolID(d.Name, d.Line),
				kind:       kindVariable,
				name:       d.Name,
				detail:     fmt.Sprintf("%s %s", typeExprString(d.Type), d.Name),
				location:   f.nextIdentLocation(d.Name, d.Line),
				typ:        f.resolveType(d.Type),
				renameable: true,
				ownerFile:  f,
			}
			f.topLevel[d.Name] = sym
		case *ast.ConstDecl:
			sym := &symbolDef{
				id:         f.symbolID(d.Name, d.Line),
				kind:       kindConstant,
				name:       d.Name,
				detail:     fmt.Sprintf("const %s %s", typeExprString(d.Type), d.Name),
				location:   f.nextIdentLocation(d.Name, d.Line),
				typ:        f.resolveType(d.Type),
				renameable: true,
				ownerFile:  f,
			}
			f.topLevel[d.Name] = sym
		case *ast.EnumDecl:
			sym := &symbolDef{
				id:         f.symbolID(d.Name, d.Line),
				kind:       kindEnum,
				name:       d.Name,
				detail:     "enum " + d.Name,
				location:   f.nextIdentLocation(d.Name, d.Line),
				renameable: true,
				ownerFile:  f,
			}
			f.topLevel[d.Name] = sym
		case *ast.InterfaceDecl:
			sym := &symbolDef{
				id:         f.symbolID(d.Name, d.Line),
				kind:       kindIface,
				name:       d.Name,
				detail:     "interface " + d.Name,
				location:   f.nextIdentLocation(d.Name, d.Line),
				renameable: true,
				ownerFile:  f,
			}
			f.topLevel[d.Name] = sym
		case *ast.ClassDecl:
			classSym := &symbolDef{
				id:         f.symbolID(d.Name, d.Line),
				kind:       kindClass,
				name:       d.Name,
				detail:     "class " + d.Name,
				location:   f.nextIdentLocation(d.Name, d.Line),
				renameable: true,
				ownerFile:  f,
			}
			class := &classInfo{
				symbol:  classSym,
				fields:  make(map[string]*symbolDef),
				methods: make(map[string]*symbolDef),
			}
			classSym.typ = typeInfo{kind: typeClassCtor, name: d.Name, class: class}
			for _, field := range d.Fields {
				fieldSym := &symbolDef{
					id:         fmt.Sprintf("%s:field:%d:%s", f.path, field.Line, field.Name),
					kind:       kindField,
					name:       field.Name,
					detail:     fmt.Sprintf("%s.%s: %s", d.Name, field.Name, typeExprString(field.Type)),
					location:   f.nextIdentLocation(field.Name, field.Line),
					typ:        f.resolveType(field.Type),
					renameable: true,
					ownerFile:  f,
				}
				class.fields[field.Name] = fieldSym
			}
			for _, method := range d.Methods {
				methodSym := &symbolDef{
					id:         fmt.Sprintf("%s:method:%d:%s", f.path, method.Line, method.Name),
					kind:       kindMethod,
					name:       method.Name,
					detail:     fnDetail(d.Name, method.Name, method.Params, method.Return),
					signature:  signatureFromDecl(d.Name, method.Name, method.Params, method.Return, ""),
					location:   f.nextIdentLocation(method.Name, method.Line),
					typ:        callableTypeFromDecl(method),
					renameable: method.Name != "init",
					ownerFile:  f,
				}
				class.methods[method.Name] = methodSym
			}
			f.classes[d.Name] = class
			f.topLevel[d.Name] = classSym
		}
	}
}

func (f *fileModel) analyze() {
	if f.analyzed {
		return
	}
	f.analyzed = true
	root := newScope(nil)
	for name, sym := range f.imports {
		root.define(name, scopeBinding{symbol: sym, typ: sym.typ})
		if sym.location != nil && sym.explicit {
			f.addOccurrence(sym, *sym.location, true)
		}
	}
	for name, sym := range f.topLevel {
		root.define(name, scopeBinding{symbol: sym, typ: sym.typ})
		if sym.location != nil {
			f.addOccurrence(sym, *sym.location, true)
		}
	}
	for _, class := range f.classes {
		for _, field := range class.fields {
			if field.location != nil {
				f.addOccurrence(field, *field.location, true)
			}
		}
		for _, method := range class.methods {
			if method.location != nil {
				f.addOccurrence(method, *method.location, true)
			}
		}
	}
	for _, decl := range f.prog.Decls {
		switch d := decl.(type) {
		case *ast.VarDecl:
			f.walkExpr(root, nil, d.Init)
		case *ast.ConstDecl:
			f.walkExpr(root, nil, d.Init)
		case *ast.FnDecl:
			f.walkFunction(root, nil, d)
		case *ast.ClassDecl:
			class := f.classes[d.Name]
			for _, method := range d.Methods {
				f.walkFunction(root, class, method)
			}
		}
	}
}

func (f *fileModel) walkFunction(parent *scope, class *classInfo, decl *ast.FnDecl) {
	scope := newScope(parent)
	for _, param := range decl.Params {
		sym := &symbolDef{
			id:         fmt.Sprintf("%s:param:%d:%s", f.path, decl.Line, param.Name),
			kind:       kindParameter,
			name:       param.Name,
			detail:     fmt.Sprintf("%s: %s", param.Name, typeExprString(param.Type)),
			location:   f.nextIdentLocation(param.Name, decl.Line),
			typ:        f.resolveType(param.Type),
			renameable: true,
			ownerFile:  f,
		}
		scope.define(param.Name, scopeBinding{symbol: sym, typ: sym.typ})
		if sym.location != nil {
			f.addOccurrence(sym, *sym.location, true)
		}
	}
	if decl.Body != nil {
		f.walkBlock(scope, class, decl.Body)
	}
}

func (f *fileModel) walkBlock(scope *scope, class *classInfo, block *ast.Block) {
	if block == nil {
		return
	}
	local := newScope(scope)
	for _, stmt := range block.Stmts {
		f.walkStmt(local, class, stmt)
	}
}

func (f *fileModel) walkStmt(scope *scope, class *classInfo, stmt ast.Stmt) {
	switch s := stmt.(type) {
	case *ast.Block:
		f.walkBlock(scope, class, s)
	case *ast.VarStmt:
		if fnLit, ok := s.Init.(*ast.FnLitExpr); ok {
			sym := &symbolDef{
				id:         fmt.Sprintf("%s:local:%d:%s", f.path, s.Line, s.Name),
				kind:       kindVariable,
				name:       s.Name,
				detail:     fnDetail("fn", s.Name, fnLit.Decl.Params, fnLit.Decl.Return),
				signature:  signatureFromDecl("fn", s.Name, fnLit.Decl.Params, fnLit.Decl.Return, ""),
				location:   f.nextIdentLocation(s.Name, s.Line),
				typ:        callableTypeFromDecl(fnLit.Decl),
				renameable: true,
				ownerFile:  f,
			}
			scope.define(s.Name, scopeBinding{symbol: sym, typ: sym.typ})
			if sym.location != nil {
				f.addOccurrence(sym, *sym.location, true)
			}
			f.walkFunction(scope, class, fnLit.Decl)
			return
		}
		sym := &symbolDef{
			id:         fmt.Sprintf("%s:local:%d:%s", f.path, s.Line, s.Name),
			kind:       kindVariable,
			name:       s.Name,
			detail:     fmt.Sprintf("%s %s", typeExprString(s.Type), s.Name),
			location:   f.nextIdentLocation(s.Name, s.Line),
			typ:        f.resolveType(s.Type),
			renameable: true,
			ownerFile:  f,
		}
		scope.define(s.Name, scopeBinding{symbol: sym, typ: sym.typ})
		if sym.location != nil {
			f.addOccurrence(sym, *sym.location, true)
		}
		f.walkExpr(scope, class, s.Init)
	case *ast.TupleBindStmt:
		f.walkExpr(scope, class, s.Value)
		for _, bind := range s.Bindings {
			if bind.Discard {
				continue
			}
			sym := &symbolDef{
				id:         fmt.Sprintf("%s:tuple:%d:%s", f.path, s.Line, bind.Name),
				kind:       kindVariable,
				name:       bind.Name,
				detail:     fmt.Sprintf("%s %s", typeExprString(bind.Type), bind.Name),
				location:   f.nextIdentLocation(bind.Name, s.Line),
				typ:        f.resolveType(bind.Type),
				renameable: true,
				ownerFile:  f,
			}
			scope.define(bind.Name, scopeBinding{symbol: sym, typ: sym.typ})
			if sym.location != nil {
				f.addOccurrence(sym, *sym.location, true)
			}
		}
	case *ast.AssignStmt:
		f.walkExpr(scope, class, s.Target)
		f.walkExpr(scope, class, s.Value)
	case *ast.ExprStmt:
		f.walkExpr(scope, class, s.Expr)
	case *ast.ReturnStmt:
		for _, expr := range s.Values {
			f.walkExpr(scope, class, expr)
		}
	case *ast.IfStmt:
		f.walkExpr(scope, class, s.Cond)
		f.walkBlock(scope, class, s.Then)
		if s.Else != nil {
			f.walkStmt(scope, class, s.Else)
		}
	case *ast.WhileStmt:
		f.walkExpr(scope, class, s.Cond)
		f.walkBlock(scope, class, s.Body)
	case *ast.ForStmt:
		loop := newScope(scope)
		if s.Init != nil {
			f.walkStmt(loop, class, s.Init)
		}
		if s.Cond != nil {
			f.walkExpr(loop, class, s.Cond)
		}
		f.walkBlock(loop, class, s.Body)
		if s.Post != nil {
			f.walkStmt(loop, class, s.Post)
		}
	case *ast.ForInStmt:
		f.walkExpr(scope, class, s.Iter)
		loop := newScope(scope)
		if s.KeyName != "" {
			sym := &symbolDef{
				id:         fmt.Sprintf("%s:forin:%d:%s", f.path, s.Line, s.KeyName),
				kind:       kindVariable,
				name:       s.KeyName,
				detail:     s.KeyName,
				location:   f.nextIdentLocation(s.KeyName, s.Line),
				renameable: true,
				ownerFile:  f,
			}
			loop.define(s.KeyName, scopeBinding{symbol: sym})
			if sym.location != nil {
				f.addOccurrence(sym, *sym.location, true)
			}
		}
		if s.ValName != "" {
			sym := &symbolDef{
				id:         fmt.Sprintf("%s:forin:%d:%s", f.path, s.Line, s.ValName),
				kind:       kindVariable,
				name:       s.ValName,
				detail:     s.ValName,
				location:   f.nextIdentLocation(s.ValName, s.Line),
				renameable: true,
				ownerFile:  f,
			}
			loop.define(s.ValName, scopeBinding{symbol: sym})
			if sym.location != nil {
				f.addOccurrence(sym, *sym.location, true)
			}
		}
		f.walkBlock(loop, class, s.Body)
	case *ast.DeferStmt:
		f.walkExpr(scope, class, s.Call)
	case *ast.GuardStmt:
		f.walkExpr(scope, class, s.Value)
		guardScope := newScope(scope)
		for _, bind := range s.Bindings {
			if bind.Discard {
				continue
			}
			sym := &symbolDef{
				id:         fmt.Sprintf("%s:guard:%d:%s", f.path, s.Line, bind.Name),
				kind:       kindVariable,
				name:       bind.Name,
				detail:     fmt.Sprintf("%s %s", typeExprString(bind.Type), bind.Name),
				location:   f.nextIdentLocation(bind.Name, s.Line),
				typ:        f.resolveType(bind.Type),
				renameable: true,
				ownerFile:  f,
			}
			guardScope.define(bind.Name, scopeBinding{symbol: sym, typ: sym.typ})
			if sym.location != nil {
				f.addOccurrence(sym, *sym.location, true)
			}
		}
		f.walkBlock(guardScope, class, s.Body)
	case *ast.SwitchStmt:
		f.walkExpr(scope, class, s.Tag)
		for _, c := range s.Cases {
			caseScope := newScope(scope)
			for _, expr := range c.Values {
				f.walkExpr(caseScope, class, expr)
			}
			for _, bodyStmt := range c.Body {
				f.walkStmt(caseScope, class, bodyStmt)
			}
		}
	case *ast.CompoundAssignStmt:
		f.walkExpr(scope, class, s.Target)
		f.walkExpr(scope, class, s.Value)
	case *ast.IncDecStmt:
		f.walkExpr(scope, class, s.Target)
	}
}

func (f *fileModel) walkExpr(scope *scope, class *classInfo, expr ast.Expr) typeInfo {
	switch e := expr.(type) {
	case nil:
		return typeInfo{kind: typeUnknown}
	case *ast.Ident:
		if binding, ok := scope.lookup(e.Name); ok {
			if loc := f.nextIdentLocation(e.Name, e.Line); loc != nil {
				f.addOccurrence(binding.symbol, *loc, false)
			}
			return binding.typ
		}
		if e.Name == "err" {
			return typeInfo{kind: typePrimitive, name: "err"}
		}
		return typeInfo{kind: typeUnknown}
	case *ast.SelfExpr:
		if class != nil {
			return typeInfo{kind: typeClassInst, name: class.symbol.name, class: class}
		}
		return typeInfo{kind: typeUnknown}
	case *ast.MemberExpr:
		objType := f.walkExpr(scope, class, e.Object)
		return f.resolveMemberAt(objType, e, nil)
	case *ast.CallExpr:
		calleeType := f.walkExpr(scope, class, e.Callee)
		for _, arg := range e.Args {
			f.walkExpr(scope, class, arg)
		}
		if calleeType.kind == typeClassCtor && calleeType.class != nil {
			return typeInfo{kind: typeClassInst, name: calleeType.class.symbol.name, class: calleeType.class}
		}
		if calleeType.callable != nil && len(calleeType.callable.ret) > 0 {
			return calleeType.callable.ret[0]
		}
		return typeInfo{kind: typeUnknown}
	case *ast.UnaryExpr:
		return f.walkExpr(scope, class, e.Operand)
	case *ast.BinaryExpr:
		f.walkExpr(scope, class, e.Left)
		f.walkExpr(scope, class, e.Right)
		return typeInfo{kind: typeUnknown}
	case *ast.TernaryExpr:
		f.walkExpr(scope, class, e.Condition)
		left := f.walkExpr(scope, class, e.TrueExpr)
		right := f.walkExpr(scope, class, e.FalseExpr)
		if left.kind != typeUnknown {
			return left
		}
		return right
	case *ast.IndexExpr:
		f.walkExpr(scope, class, e.Object)
		f.walkExpr(scope, class, e.Index)
		return typeInfo{kind: typeUnknown}
	case *ast.ArrayLit:
		for _, elem := range e.Elems {
			f.walkExpr(scope, class, elem)
		}
		return typeInfo{kind: typePrimitive, name: "array"}
	case *ast.MapLit:
		for _, key := range e.Keys {
			f.walkExpr(scope, class, key)
		}
		for _, val := range e.Values {
			f.walkExpr(scope, class, val)
		}
		return typeInfo{kind: typePrimitive, name: "map"}
	case *ast.TupleExpr:
		for _, elem := range e.Elems {
			f.walkExpr(scope, class, elem)
		}
		return typeInfo{kind: typeUnknown}
	case *ast.TypeConvExpr:
		f.walkExpr(scope, class, e.Arg)
		return f.resolveType(e.Target)
	case *ast.ErrExpr:
		f.walkExpr(scope, class, e.Msg)
		f.walkExpr(scope, class, e.Kind)
		return typeInfo{kind: typePrimitive, name: "err"}
	case *ast.FnLitExpr:
		return callableTypeFromDecl(e.Decl)
	case *ast.FStringExpr:
		for _, part := range e.Parts {
			if part.IsExpr {
				if !f.walkFStringPart(scope, class, part) {
					f.walkExpr(scope, class, part.Expr)
				}
			}
		}
		return typeInfo{kind: typePrimitive, name: "string"}
	case *ast.IntLit:
		return typeInfo{kind: typePrimitive, name: "i64"}
	case *ast.FloatLit:
		return typeInfo{kind: typePrimitive, name: "f64"}
	case *ast.StringLit:
		return typeInfo{kind: typePrimitive, name: "string"}
	case *ast.BoolLit:
		return typeInfo{kind: typePrimitive, name: "bool"}
	default:
		return typeInfo{kind: typeUnknown}
	}
}

func (f *fileModel) resolveMember(objType typeInfo, expr *ast.MemberExpr) typeInfo {
	return f.resolveMemberAt(objType, expr, nil)
}

func (f *fileModel) resolveMemberAt(objType typeInfo, expr *ast.MemberExpr, loc *Location) typeInfo {
	if loc == nil {
		loc = f.nextIdentLocation(expr.Field, expr.Line)
	}
	switch {
	case objType.kind == typeModule && objType.module != nil:
		if sym, ok := objType.module.topLevel[expr.Field]; ok {
			if loc != nil {
				f.addOccurrence(sym, *loc, false)
			}
			return sym.typ
		}
	case objType.kind == typeModule && objType.builtin != "":
		if loc != nil {
			sym := &symbolDef{
				id:       fmt.Sprintf("builtin:%s:%s", objType.builtin, expr.Field),
				kind:     kindFunction,
				name:     expr.Field,
				detail:   fmt.Sprintf("%s.%s", objType.builtin, expr.Field),
				location: nil,
			}
			if builtinSym, ok := f.analyzer.builtinMember(objType.builtin, expr.Field); ok {
				sym.detail = builtinSym.Detail
				sym.doc = builtinSym.Documentation
				sym.signature = builtinSignatureInfo(builtinSym)
				sym.typ = builtinTypeFromMetadata(builtinSym)
			}
			f.addOccurrence(sym, *loc, false)
		}
		return typeInfo{kind: typeUnknown}
	case objType.kind == typeClassInst && objType.class != nil:
		if sym, ok := objType.class.fields[expr.Field]; ok {
			if loc != nil {
				f.addOccurrence(sym, *loc, false)
			}
			return sym.typ
		}
		if sym, ok := objType.class.methods[expr.Field]; ok {
			if loc != nil {
				f.addOccurrence(sym, *loc, false)
			}
			return sym.typ
		}
	}
	return typeInfo{kind: typeUnknown}
}

func (f *fileModel) walkFStringPart(scope *scope, class *classInfo, part ast.FStringPart) bool {
	if part.ExprSource == "" || part.ExprLine <= 0 || part.ExprCol <= 0 {
		return false
	}
	tokens, err := lexer.New(part.ExprSource).Tokenize()
	if err != nil {
		return false
	}
	cursor := exprTokenCursor{path: f.path, tokens: adjustExprTokens(tokens, part.ExprLine, part.ExprCol)}
	f.walkExprWithCursor(scope, class, part.Expr, &cursor)
	return true
}

func adjustExprTokens(tokens []lexer.Token, line, col int) []lexer.Token {
	out := make([]lexer.Token, len(tokens))
	copy(out, tokens)
	for i := range out {
		if out[i].Type == lexer.TOKEN_EOF {
			continue
		}
		if out[i].Line == 1 {
			out[i].Line = line
			out[i].Col = col + out[i].Col - 1
			continue
		}
		out[i].Line = line + out[i].Line - 1
	}
	return out
}

func (c *exprTokenCursor) nextIdentLocation(name string) *Location {
	for i := c.pos; i < len(c.tokens); i++ {
		tok := c.tokens[i]
		if tok.Type == lexer.TOKEN_EOF {
			break
		}
		if tok.Literal == name && isIdentifierish(tok.Type) {
			c.pos = i + 1
			return tokenLocation(c.path, tok)
		}
	}
	for _, tok := range c.tokens {
		if tok.Literal == name && isIdentifierish(tok.Type) {
			return tokenLocation(c.path, tok)
		}
	}
	return nil
}

func (f *fileModel) walkExprWithCursor(scope *scope, class *classInfo, expr ast.Expr, cursor *exprTokenCursor) typeInfo {
	switch e := expr.(type) {
	case nil:
		return typeInfo{kind: typeUnknown}
	case *ast.Ident:
		if binding, ok := scope.lookup(e.Name); ok {
			if loc := cursor.nextIdentLocation(e.Name); loc != nil {
				f.addOccurrence(binding.symbol, *loc, false)
			}
			return binding.typ
		}
		if e.Name == "err" {
			return typeInfo{kind: typePrimitive, name: "err"}
		}
		return typeInfo{kind: typeUnknown}
	case *ast.SelfExpr:
		if class != nil {
			return typeInfo{kind: typeClassInst, name: class.symbol.name, class: class}
		}
		return typeInfo{kind: typeUnknown}
	case *ast.MemberExpr:
		objType := f.walkExprWithCursor(scope, class, e.Object, cursor)
		return f.resolveMemberAt(objType, e, cursor.nextIdentLocation(e.Field))
	case *ast.CallExpr:
		calleeType := f.walkExprWithCursor(scope, class, e.Callee, cursor)
		for _, arg := range e.Args {
			f.walkExprWithCursor(scope, class, arg, cursor)
		}
		if calleeType.kind == typeClassCtor && calleeType.class != nil {
			return typeInfo{kind: typeClassInst, name: calleeType.class.symbol.name, class: calleeType.class}
		}
		if calleeType.callable != nil && len(calleeType.callable.ret) > 0 {
			return calleeType.callable.ret[0]
		}
		return typeInfo{kind: typeUnknown}
	case *ast.UnaryExpr:
		return f.walkExprWithCursor(scope, class, e.Operand, cursor)
	case *ast.BinaryExpr:
		f.walkExprWithCursor(scope, class, e.Left, cursor)
		f.walkExprWithCursor(scope, class, e.Right, cursor)
		return typeInfo{kind: typeUnknown}
	case *ast.TernaryExpr:
		f.walkExprWithCursor(scope, class, e.Condition, cursor)
		left := f.walkExprWithCursor(scope, class, e.TrueExpr, cursor)
		right := f.walkExprWithCursor(scope, class, e.FalseExpr, cursor)
		if left.kind != typeUnknown {
			return left
		}
		return right
	case *ast.IndexExpr:
		f.walkExprWithCursor(scope, class, e.Object, cursor)
		f.walkExprWithCursor(scope, class, e.Index, cursor)
		return typeInfo{kind: typeUnknown}
	case *ast.ArrayLit:
		for _, elem := range e.Elems {
			f.walkExprWithCursor(scope, class, elem, cursor)
		}
		return typeInfo{kind: typePrimitive, name: "array"}
	case *ast.MapLit:
		for _, key := range e.Keys {
			f.walkExprWithCursor(scope, class, key, cursor)
		}
		for _, val := range e.Values {
			f.walkExprWithCursor(scope, class, val, cursor)
		}
		return typeInfo{kind: typePrimitive, name: "map"}
	case *ast.TupleExpr:
		for _, elem := range e.Elems {
			f.walkExprWithCursor(scope, class, elem, cursor)
		}
		return typeInfo{kind: typeUnknown}
	case *ast.TypeConvExpr:
		f.walkExprWithCursor(scope, class, e.Arg, cursor)
		return f.resolveType(e.Target)
	case *ast.ErrExpr:
		f.walkExprWithCursor(scope, class, e.Msg, cursor)
		f.walkExprWithCursor(scope, class, e.Kind, cursor)
		return typeInfo{kind: typePrimitive, name: "err"}
	case *ast.FnLitExpr:
		return callableTypeFromDecl(e.Decl)
	case *ast.FStringExpr:
		for _, nested := range e.Parts {
			if nested.IsExpr {
				f.walkExprWithCursor(scope, class, nested.Expr, cursor)
			}
		}
		return typeInfo{kind: typePrimitive, name: "string"}
	case *ast.IntLit:
		return typeInfo{kind: typePrimitive, name: "i64"}
	case *ast.FloatLit:
		return typeInfo{kind: typePrimitive, name: "f64"}
	case *ast.StringLit:
		return typeInfo{kind: typePrimitive, name: "string"}
	case *ast.BoolLit:
		return typeInfo{kind: typePrimitive, name: "bool"}
	default:
		return typeInfo{kind: typeUnknown}
	}
}

func (f *fileModel) resolveType(typ *ast.TypeExpr) typeInfo {
	if typ == nil {
		return typeInfo{kind: typeUnknown}
	}
	switch typ.Name {
	case "", "array", "map", "bool", "i32", "i64", "f64", "u8", "u32", "u64", "string", "void", "err":
		if typ.Name == "fn" {
			return typeInfo{kind: typeCallable, name: "fn"}
		}
		return typeInfo{kind: typePrimitive, name: typ.Name}
	case "fn":
		return typeInfo{
			kind: typeCallable,
			name: "fn",
			callable: &callableInfo{
				params: typeExprStrings(typ.ParamTypes),
				ret:    []typeInfo{f.resolveType(typ.ReturnType)},
			},
		}
	default:
		if strings.Contains(typ.Name, ".") {
			parts := strings.SplitN(typ.Name, ".", 2)
			if imp, ok := f.imports[parts[0]]; ok && imp.typ.module != nil {
				if class, ok := imp.typ.module.classes[parts[1]]; ok {
					return typeInfo{kind: typeClassInst, name: parts[1], class: class}
				}
			}
		}
		if class, ok := f.classes[typ.Name]; ok {
			return typeInfo{kind: typeClassInst, name: typ.Name, class: class}
		}
		if sym, ok := f.topLevel[typ.Name]; ok && sym.kind == kindClass && sym.typ.class != nil {
			return typeInfo{kind: typeClassInst, name: typ.Name, class: sym.typ.class}
		}
		return typeInfo{kind: typeUnknown, name: typ.Name}
	}
}

func (f *fileModel) addOccurrence(sym *symbolDef, loc Location, isDecl bool) {
	f.occurrences = append(f.occurrences, occurrence{
		symbol:   sym,
		location: loc,
		isDecl:   isDecl,
	})
}

func (f *fileModel) symbolID(name string, line int) string {
	return fmt.Sprintf("%s:%d:%s", f.path, line, name)
}

func (f *fileModel) nextIdentLocation(name string, line int) *Location {
	for i := f.tokenPos; i < len(f.tokens); i++ {
		tok := f.tokens[i]
		if tok.Line < line {
			continue
		}
		if tok.Line > line+1 {
			break
		}
		if tok.Literal == name && isIdentifierish(tok.Type) {
			f.tokenPos = i + 1
			return tokenLocation(f.path, tok)
		}
	}
	for _, tok := range f.tokens {
		if tok.Line == line && tok.Literal == name && isIdentifierish(tok.Type) {
			return tokenLocation(f.path, tok)
		}
	}
	return nil
}

func (f *fileModel) occurrenceAt(pos Position) *occurrence {
	var adjacent *occurrence
	for i := range f.occurrences {
		item := &f.occurrences[i]
		if item.location.Line != pos.Line {
			continue
		}
		if pos.Col >= item.location.Col && pos.Col <= item.location.EndCol {
			return item
		}
		if pos.Col == item.location.EndCol+1 || pos.Col+1 == item.location.Col {
			adjacent = item
		}
	}
	return adjacent
}

func (f *fileModel) memberCompletions(name string) ([]CompletionItem, error) {
	if sym, ok := f.imports[name]; ok {
		if sym.typ.module != nil {
			var items []CompletionItem
			for memberName, member := range sym.typ.module.topLevel {
				items = append(items, CompletionItem{
					Label:         memberName,
					Kind:          string(member.kind),
					Detail:        member.detail,
					Documentation: member.doc,
				})
			}
			sort.Slice(items, func(i, j int) bool { return items[i].Label < items[j].Label })
			return items, nil
		}
		if sym.typ.builtin != "" {
			var items []CompletionItem
			for _, member := range f.analyzer.builtinCompletions[sym.typ.builtin] {
				detail := fmt.Sprintf("%s.%s", sym.typ.builtin, member)
				doc := ""
				if builtinSym, ok := f.analyzer.builtinMember(sym.typ.builtin, member); ok {
					detail = builtinSym.Detail
					doc = builtinSym.Documentation
				}
				items = append(items, CompletionItem{
					Label:         member,
					Kind:          string(kindFunction),
					Detail:        detail,
					Documentation: doc,
				})
			}
			sort.Slice(items, func(i, j int) bool { return items[i].Label < items[j].Label })
			return items, nil
		}
	}

	for _, occ := range f.occurrences {
		if occ.symbol == nil || occ.symbol.name != name || occ.symbol.kind == kindImport {
			continue
		}
		if occ.symbol.typ.class != nil {
			var items []CompletionItem
			for memberName, member := range occ.symbol.typ.class.fields {
				items = append(items, CompletionItem{Label: memberName, Kind: string(member.kind), Detail: member.detail, Documentation: member.doc})
			}
			for memberName, member := range occ.symbol.typ.class.methods {
				items = append(items, CompletionItem{Label: memberName, Kind: string(member.kind), Detail: member.detail, Documentation: member.doc})
			}
			sort.Slice(items, func(i, j int) bool { return items[i].Label < items[j].Label })
			return items, nil
		}
	}
	return nil, nil
}

func callableTypeFromDecl(decl *ast.FnDecl) typeInfo {
	var returns []typeInfo
	if decl.Return != nil {
		for _, ret := range decl.Return.Types {
			returns = append(returns, typeInfo{kind: typePrimitive, name: typeExprString(ret)})
		}
	}
	return typeInfo{
		kind: typeCallable,
		name: decl.Name,
		callable: &callableInfo{
			params: paramDetails(decl.Params),
			ret:    returns,
		},
	}
}

func fnDetail(owner, name string, params []ast.Param, ret *ast.ReturnType) string {
	var b strings.Builder
	if owner == "fn" {
		fmt.Fprintf(&b, "fn %s(", name)
	} else {
		fmt.Fprintf(&b, "%s.%s(", owner, name)
	}
	for i, param := range params {
		if i > 0 {
			b.WriteString(", ")
		}
		fmt.Fprintf(&b, "%s %s", typeExprString(param.Type), param.Name)
	}
	b.WriteString(")")
	if ret != nil && len(ret.Types) > 0 {
		b.WriteString(" -> ")
		if len(ret.Types) == 1 {
			b.WriteString(typeExprString(ret.Types[0]))
		} else {
			var parts []string
			for _, item := range ret.Types {
				parts = append(parts, typeExprString(item))
			}
			b.WriteString("(" + strings.Join(parts, ", ") + ")")
		}
	}
	return b.String()
}

func paramDetails(params []ast.Param) []string {
	out := make([]string, 0, len(params))
	for _, param := range params {
		out = append(out, fmt.Sprintf("%s %s", typeExprString(param.Type), param.Name))
	}
	return out
}

func typeExprStrings(types []*ast.TypeExpr) []string {
	out := make([]string, 0, len(types))
	for _, typ := range types {
		out = append(out, typeExprString(typ))
	}
	return out
}

func typeExprString(typ *ast.TypeExpr) string {
	if typ == nil {
		return "unknown"
	}
	switch typ.Name {
	case "array":
		return "array<" + typeExprString(typ.ElemType) + ">"
	case "map":
		return "map<" + typeExprString(typ.KeyType) + ", " + typeExprString(typ.ValType) + ">"
	case "fn":
		var params []string
		for _, item := range typ.ParamTypes {
			params = append(params, typeExprString(item))
		}
		if typ.ReturnType != nil {
			return "fn(" + strings.Join(params, ", ") + ") -> " + typeExprString(typ.ReturnType)
		}
		if len(params) == 0 {
			return "fn"
		}
		return "fn(" + strings.Join(params, ", ") + ")"
	default:
		return typ.Name
	}
}

func tokenLocation(path string, tok lexer.Token) *Location {
	return &Location{
		Path:   path,
		Line:   tok.Line,
		Col:    tok.Col,
		EndCol: tok.Col + len(tok.Literal) - 1,
	}
}

func isIdentifierish(tt lexer.TokenType) bool {
	switch tt {
	case lexer.TOKEN_IDENT,
		lexer.TOKEN_BOOL_TYPE,
		lexer.TOKEN_I32,
		lexer.TOKEN_I64,
		lexer.TOKEN_F64,
		lexer.TOKEN_U8,
		lexer.TOKEN_U32,
		lexer.TOKEN_U64,
		lexer.TOKEN_STRING_TYPE,
		lexer.TOKEN_VOID,
		lexer.TOKEN_ERR:
		return true
	default:
		return false
	}
}

func resolveSemanticSearchPaths(path string, extraSearchPaths []string) ([]string, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}
	var out []string
	addUniquePath(&out, filepath.Dir(absPath))
	projectRoot, ok, err := findProjectRoot(absPath)
	if err != nil {
		return nil, err
	}
	if ok {
		addUniquePath(&out, filepath.Join(projectRoot, "lib"))
		addUniquePath(&out, filepath.Join(projectRoot, "deps"))
	}
	for _, item := range extraSearchPaths {
		abs, err := filepath.Abs(item)
		if err != nil {
			return nil, err
		}
		addUniquePath(&out, abs)
	}
	return out, nil
}

func resolveImportPath(name string, searchPaths []string) (string, error) {
	fileName := name + ".basl"
	for _, dir := range searchPaths {
		fullPath := filepath.Join(dir, fileName)
		if info, err := os.Stat(fullPath); err == nil && !info.IsDir() {
			return filepath.Abs(fullPath)
		}
		pkgPath := filepath.Join(dir, name, "lib", fileName)
		if info, err := os.Stat(pkgPath); err == nil && !info.IsDir() {
			return filepath.Abs(pkgPath)
		}
	}
	return "", fmt.Errorf("module %q not found", name)
}

func findProjectRoot(path string) (string, bool, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return "", false, err
	}
	start := absPath
	if info, err := os.Stat(absPath); err == nil && !info.IsDir() {
		start = filepath.Dir(absPath)
	}
	for {
		manifest := filepath.Join(start, "basl.toml")
		if info, err := os.Stat(manifest); err == nil && !info.IsDir() {
			return start, true, nil
		}
		parent := filepath.Dir(start)
		if parent == start {
			return "", false, nil
		}
		start = parent
	}
}

func addUniquePath(paths *[]string, path string) {
	if path == "" {
		return
	}
	for _, existing := range *paths {
		if existing == path {
			return
		}
	}
	*paths = append(*paths, path)
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

func workspaceFileCandidates(entryPath string, searchPaths []string) []string {
	seen := make(map[string]bool)
	var files []string
	add := func(path string) {
		if path == "" || seen[path] {
			return
		}
		seen[path] = true
		files = append(files, path)
	}

	add(entryPath)
	projectRoot, ok, _ := findProjectRoot(entryPath)
	if ok {
		addBaslFiles(&files, seen, filepath.Join(projectRoot, "lib"))
		addBaslFiles(&files, seen, filepath.Join(projectRoot, "test"))
		mainPath := filepath.Join(projectRoot, "main.basl")
		if _, err := os.Stat(mainPath); err == nil {
			add(mainPath)
		}
	}
	for _, root := range searchPaths {
		addBaslFiles(&files, seen, root)
	}
	sort.Strings(files)
	return files
}

func addBaslFiles(files *[]string, seen map[string]bool, root string) {
	info, err := os.Stat(root)
	if err != nil || !info.IsDir() {
		return
	}
	_ = filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil
		}
		if info.IsDir() {
			return nil
		}
		if filepath.Ext(path) != ".basl" {
			return nil
		}
		absPath, err := filepath.Abs(path)
		if err != nil || seen[absPath] {
			return nil
		}
		seen[absPath] = true
		*files = append(*files, absPath)
		return nil
	})
}

func prefixAt(src string, pos Position) string {
	lines := strings.Split(src, "\n")
	if pos.Line <= 0 || pos.Line > len(lines) {
		return ""
	}
	line := lines[pos.Line-1]
	col := pos.Col - 1
	if col < 0 {
		col = 0
	}
	if col > len(line) {
		col = len(line)
	}
	return line[:col]
}

func loadBuiltinCompletions() (map[string][]string, error) {
	data, err := editorassets.Assets.ReadFile("vscode/completions.json")
	if err != nil {
		return nil, err
	}
	var out map[string][]string
	if err := json.Unmarshal(data, &out); err != nil {
		return nil, err
	}
	return out, nil
}

func dedupeLocations(items []Location) []Location {
	seen := make(map[string]bool)
	var out []Location
	for _, item := range items {
		key := fmt.Sprintf("%s:%d:%d:%d", item.Path, item.Line, item.Col, item.EndCol)
		if seen[key] {
			continue
		}
		seen[key] = true
		out = append(out, item)
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Path != out[j].Path {
			return out[i].Path < out[j].Path
		}
		if out[i].Line != out[j].Line {
			return out[i].Line < out[j].Line
		}
		return out[i].Col < out[j].Col
	})
	return out
}

func dedupeEdits(items []RenameEdit) []RenameEdit {
	seen := make(map[string]bool)
	var out []RenameEdit
	for _, item := range items {
		key := fmt.Sprintf("%s:%d:%d:%d", item.Path, item.Line, item.Col, item.EndCol)
		if seen[key] {
			continue
		}
		seen[key] = true
		out = append(out, item)
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Path != out[j].Path {
			return out[i].Path < out[j].Path
		}
		if out[i].Line != out[j].Line {
			return out[i].Line < out[j].Line
		}
		return out[i].Col < out[j].Col
	})
	return out
}
