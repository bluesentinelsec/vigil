package editor

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/formatter"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

type ParameterInformation struct {
	Label         string `json:"label"`
	Documentation string `json:"documentation,omitempty"`
}

type SignatureInformation struct {
	Label         string                 `json:"label"`
	Documentation string                 `json:"documentation,omitempty"`
	Parameters    []ParameterInformation `json:"parameters,omitempty"`
}

type SignatureHelp struct {
	Signatures      []SignatureInformation `json:"signatures"`
	ActiveSignature int                    `json:"activeSignature"`
	ActiveParameter int                    `json:"activeParameter"`
}

type PrepareRename struct {
	Range       Location `json:"range"`
	Placeholder string   `json:"placeholder"`
}

var identifierPattern = regexp.MustCompile(`^[A-Za-z_][A-Za-z0-9_]*$`)
var unknownIdentifierDiagnosticPattern = regexp.MustCompile(`unknown identifier "([^"]+)"`)

type DiagnosticHint struct {
	Message string    `json:"message"`
	Range   *Location `json:"range,omitempty"`
}

type CodeAction struct {
	Title       string       `json:"title"`
	Kind        string       `json:"kind,omitempty"`
	IsPreferred bool         `json:"is_preferred,omitempty"`
	Diagnostics []string     `json:"diagnostics,omitempty"`
	Edits       []RenameEdit `json:"edits,omitempty"`
}

type FoldingRange struct {
	StartLine int    `json:"start_line"`
	EndLine   int    `json:"end_line"`
	Kind      string `json:"kind,omitempty"`
}

func validIdentifier(name string) bool {
	return identifierPattern.MatchString(name)
}

func (a *Analyzer) builtinModule(name string) (builtinModule, bool) {
	if a == nil || a.builtinDocs == nil {
		return builtinModule{}, false
	}
	mod, ok := a.builtinDocs.Modules[name]
	return mod, ok
}

func (a *Analyzer) builtinMember(moduleName, memberName string) (builtinSymbol, bool) {
	mod, ok := a.builtinModule(moduleName)
	if !ok {
		return builtinSymbol{}, false
	}
	item, ok := mod.Members[memberName]
	return item, ok
}

func builtinSignatureInfo(item builtinSymbol) *signatureInfo {
	params := make([]builtinParam, 0, len(item.Params))
	params = append(params, item.Params...)
	return &signatureInfo{
		label:         item.Signature,
		documentation: item.Documentation,
		params:        params,
	}
}

func builtinTypeFromMetadata(item builtinSymbol) typeInfo {
	callable := &callableInfo{
		params: make([]string, 0, len(item.Params)),
	}
	for _, param := range item.Params {
		callable.params = append(callable.params, param.Label)
	}
	return typeInfo{kind: typeCallable, callable: callable}
}

func signatureFromDecl(owner, name string, params []ast.Param, ret *ast.ReturnType, doc string) *signatureInfo {
	out := &signatureInfo{
		label:         fnDetail(owner, name, params, ret),
		documentation: doc,
		params:        make([]builtinParam, 0, len(params)),
	}
	for _, param := range params {
		out.params = append(out.params, builtinParam{Label: fmt.Sprintf("%s %s", typeExprString(param.Type), param.Name)})
	}
	return out
}

func PrepareRenameAt(path string, pos Position, extraSearchPaths []string) (*PrepareRename, error) {
	return PrepareRenameAtWithOptions(path, pos, Options{SearchPaths: extraSearchPaths})
}

func PrepareRenameAtWithOptions(path string, pos Position, opts Options) (*PrepareRename, error) {
	_, file, err := newAnalyzerWithOptions(path, opts)
	if err != nil {
		return nil, err
	}
	occ := file.occurrenceAt(pos)
	if occ == nil || occ.symbol == nil {
		return nil, fmt.Errorf("no symbol at position")
	}
	if !occ.symbol.renameable {
		return nil, fmt.Errorf("symbol %q cannot be renamed", occ.symbol.name)
	}
	return &PrepareRename{
		Range:       occ.location,
		Placeholder: occ.symbol.name,
	}, nil
}

func SignatureHelpAt(path string, pos Position, extraSearchPaths []string) (*SignatureHelp, error) {
	return SignatureHelpAtWithOptions(path, pos, Options{SearchPaths: extraSearchPaths})
}

func SignatureHelpAtWithOptions(path string, pos Position, opts Options) (*SignatureHelp, error) {
	_, file, err := newAnalyzerWithOptions(path, opts)
	if err != nil {
		return nil, err
	}
	call, ok := activeCallAt(file.src, pos)
	if !ok {
		return nil, nil
	}
	calleePos := positionFromIndex(file.src, call.nameStart)
	occ := file.occurrenceAt(calleePos)
	if occ == nil || occ.symbol == nil || occ.symbol.signature == nil {
		return nil, nil
	}
	sig := occ.symbol.signature
	params := make([]ParameterInformation, 0, len(sig.params))
	for _, param := range sig.params {
		params = append(params, ParameterInformation{Label: param.Label})
	}
	activeParam := call.argIndex
	if activeParam >= len(params) && len(params) > 0 {
		activeParam = len(params) - 1
	}
	return &SignatureHelp{
		Signatures: []SignatureInformation{{
			Label:         sig.label,
			Documentation: sig.documentation,
			Parameters:    params,
		}},
		ActiveSignature: 0,
		ActiveParameter: activeParam,
	}, nil
}

type activeCall struct {
	nameStart int
	argIndex  int
}

func activeCallAt(src string, pos Position) (activeCall, bool) {
	idx := positionToIndex(src, pos)
	if idx < 0 || idx > len(src) {
		return activeCall{}, false
	}

	type callState struct {
		open    int
		commas  int
		inAngle int
	}
	var stack []callState
	inString := false
	escaped := false
	for i := 0; i < idx; i++ {
		ch := src[i]
		if inString {
			if escaped {
				escaped = false
				continue
			}
			if ch == '\\' {
				escaped = true
				continue
			}
			if ch == '"' {
				inString = false
			}
			continue
		}
		switch ch {
		case '"':
			inString = true
		case '(':
			stack = append(stack, callState{open: i})
		case ')':
			if len(stack) > 0 {
				stack = stack[:len(stack)-1]
			}
		case '<':
			if len(stack) > 0 {
				stack[len(stack)-1].inAngle++
			}
		case '>':
			if len(stack) > 0 && stack[len(stack)-1].inAngle > 0 {
				stack[len(stack)-1].inAngle--
			}
		case ',':
			if len(stack) > 0 && stack[len(stack)-1].inAngle == 0 {
				stack[len(stack)-1].commas++
			}
		}
	}
	if len(stack) == 0 {
		return activeCall{}, false
	}
	state := stack[len(stack)-1]
	end := state.open
	for end > 0 && (src[end-1] == ' ' || src[end-1] == '\t' || src[end-1] == '\n' || src[end-1] == '\r') {
		end--
	}
	start := end
	for start > 0 {
		ch := src[start-1]
		if (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' {
			start--
			continue
		}
		break
	}
	if start == end {
		return activeCall{}, false
	}
	nameStart := start
	if dot := strings.LastIndex(src[start:end], "."); dot >= 0 {
		nameStart = start + dot + 1
	}
	return activeCall{nameStart: nameStart, argIndex: state.commas}, true
}

func positionToIndex(src string, pos Position) int {
	line := 1
	col := 1
	for i, r := range src {
		if line == pos.Line && col == pos.Col {
			return i
		}
		if r == '\n' {
			line++
			col = 1
		} else {
			col++
		}
	}
	if line == pos.Line && col == pos.Col {
		return len(src)
	}
	return len(src)
}

func positionFromIndex(src string, idx int) Position {
	line := 1
	col := 1
	for i, r := range src {
		if i >= idx {
			break
		}
		if r == '\n' {
			line++
			col = 1
		} else {
			col++
		}
	}
	return Position{Line: line, Col: col}
}

func CodeActions(path string, diagnostics []string, only []string, extraSearchPaths []string) ([]CodeAction, error) {
	hints := make([]DiagnosticHint, 0, len(diagnostics))
	for _, item := range diagnostics {
		hints = append(hints, DiagnosticHint{Message: item})
	}
	return codeActionsWithDiagnostics(path, hints, only, nil, Options{SearchPaths: extraSearchPaths})
}

func CodeActionsWithOptions(path string, diagnostics []string, only []string, opts Options) ([]CodeAction, error) {
	hints := make([]DiagnosticHint, 0, len(diagnostics))
	for _, item := range diagnostics {
		hints = append(hints, DiagnosticHint{Message: item})
	}
	return codeActionsWithDiagnostics(path, hints, only, nil, opts)
}

func CodeActionsWithDiagnostics(path string, diagnostics []DiagnosticHint, only []string, targetRange *Location, extraSearchPaths []string) ([]CodeAction, error) {
	return codeActionsWithDiagnostics(path, diagnostics, only, targetRange, Options{SearchPaths: extraSearchPaths})
}

func CodeActionsWithDiagnosticsAndOptions(path string, diagnostics []DiagnosticHint, only []string, targetRange *Location, opts Options) ([]CodeAction, error) {
	return codeActionsWithDiagnostics(path, diagnostics, only, targetRange, opts)
}

func codeActionsWithDiagnostics(path string, diagnostics []DiagnosticHint, only []string, targetRange *Location, opts Options) ([]CodeAction, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}
	src, err := readDocumentSource(absPath, opts)
	if err != nil {
		return nil, err
	}
	a, file, err := newAnalyzerWithOptions(absPath, opts)
	if err != nil {
		return nil, err
	}

	var actions []CodeAction
	if action, err := organizeImportsAction(absPath, src, only); err != nil {
		return nil, err
	} else if action != nil {
		actions = append(actions, *action)
	}

	if wantsCodeActionKind("quickfix", only) {
		actions = append(actions, missingImportActions(a, file, diagnostics, targetRange, src)...)
	}
	return dedupeCodeActions(actions), nil
}

func FoldingRanges(path string, extraSearchPaths []string) ([]FoldingRange, error) {
	return FoldingRangesWithOptions(path, Options{SearchPaths: extraSearchPaths})
}

func FoldingRangesWithOptions(path string, opts Options) ([]FoldingRange, error) {
	src, err := readDocumentSource(path, opts)
	if err != nil {
		return nil, err
	}
	tokens, err := lexer.New(src).TokenizeWithComments()
	if err != nil {
		return nil, err
	}
	var out []FoldingRange
	out = append(out, importFoldingRanges(src)...)
	out = append(out, commentFoldingRanges(tokens)...)
	out = append(out, braceFoldingRanges(tokens)...)
	return dedupeFoldingRanges(out), nil
}

func FormatDocument(path string, extraSearchPaths []string) (string, error) {
	return FormatDocumentWithOptions(path, Options{SearchPaths: extraSearchPaths})
}

func FormatDocumentWithOptions(path string, opts Options) (string, error) {
	src, err := readDocumentSource(path, opts)
	if err != nil {
		return "", err
	}
	out, err := formatSource(src)
	if err != nil {
		return "", err
	}
	return string(out), nil
}

func readDocumentSource(path string, opts Options) (string, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return "", err
	}
	overlays := normalizeOverlays(opts.Overlays)
	if src, ok := overlays[absPath]; ok {
		return src, nil
	}
	data, err := os.ReadFile(absPath)
	if err != nil {
		return "", err
	}
	return string(data), nil
}

func formatSource(src string) ([]byte, error) {
	tokensWithComments, err := lexer.New(src).TokenizeWithComments()
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
	out := formatter.Format(prog, tokensWithComments)
	if bytes.Equal(out, []byte(src)) {
		return []byte(src), nil
	}
	return out, nil
}

func organizeImportsAction(path string, src string, only []string) (*CodeAction, error) {
	if !wantsCodeActionKind("source.organizeImports", only) {
		return nil, nil
	}
	if !strings.Contains(src, "import ") {
		return nil, nil
	}
	formatted, err := formatSource(src)
	if err != nil {
		return nil, err
	}
	if string(formatted) == src {
		return nil, nil
	}
	return &CodeAction{
		Title: "Organize Imports",
		Kind:  "source.organizeImports",
		Edits: []RenameEdit{{
			Path:    path,
			Line:    1,
			Col:     1,
			EndCol:  1,
			NewText: string(formatted),
		}},
	}, nil
}

func missingImportActions(a *Analyzer, file *fileModel, diagnostics []DiagnosticHint, targetRange *Location, src string) []CodeAction {
	modules := a.importableModules(file)
	if len(modules) == 0 {
		return nil
	}
	var actions []CodeAction
	seen := make(map[string]bool)
	for _, diag := range diagnostics {
		match := unknownIdentifierDiagnosticPattern.FindStringSubmatch(diag.Message)
		if len(match) != 2 {
			continue
		}
		name := match[1]
		for _, module := range modules {
			importEdit, err := insertImportEdit(file.path, src, module.Path)
			if err != nil {
				continue
			}
			if module.Alias == name {
				key := "import:" + name + ":" + module.Path
				if seen[key] {
					continue
				}
				seen[key] = true
				actions = append(actions, CodeAction{
					Title:       fmt.Sprintf("Add import %q", module.Path),
					Kind:        "quickfix",
					IsPreferred: true,
					Diagnostics: []string{diag.Message},
					Edits:       []RenameEdit{importEdit},
				})
				continue
			}
			member, ok := module.memberByName(name)
			if !ok {
				continue
			}
			replacementRange := findUnknownIdentifierRange(src, diag, targetRange, name)
			if replacementRange == nil {
				continue
			}
			key := "qualify:" + name + ":" + module.Path
			if seen[key] {
				continue
			}
			seen[key] = true
			actions = append(actions, CodeAction{
				Title:       fmt.Sprintf("Import %q and use %s.%s", module.Path, module.Alias, name),
				Kind:        "quickfix",
				IsPreferred: true,
				Diagnostics: []string{diag.Message},
				Edits: []RenameEdit{
					importEdit,
					{
						Path:    replacementRange.Path,
						Line:    replacementRange.Line,
						Col:     replacementRange.Col,
						EndCol:  replacementRange.EndCol,
						NewText: module.Alias + "." + member.Label,
					},
				},
			})
		}
	}
	return actions
}

type importableModule struct {
	Path          string
	Alias         string
	Documentation string
	members       []CompletionItem
}

func (a *Analyzer) importableModules(file *fileModel) []importableModule {
	var out []importableModule
	seen := make(map[string]bool)
	for name := range a.builtinCompletions {
		if _, imported := file.imports[name]; imported {
			continue
		}
		module := importableModule{Path: name, Alias: name}
		if mod, ok := a.builtinModule(name); ok {
			module.Documentation = mod.Summary
			for _, memberName := range a.builtinCompletions[name] {
				item := CompletionItem{
					Label:  memberName,
					Kind:   string(kindFunction),
					Detail: fmt.Sprintf("%s.%s", name, memberName),
				}
				if builtinSym, ok := a.builtinMember(name, memberName); ok {
					item.Detail = builtinSym.Detail
					item.Documentation = builtinSym.Documentation
				}
				module.members = append(module.members, item)
			}
		}
		out = append(out, module)
		seen[module.Alias] = true
	}
	for _, candidate := range workspaceFileCandidates(file.path, a.searchPaths) {
		if sameDocumentPath(candidate, file.path) {
			continue
		}
		modulePath := moduleImportPath(candidate, a.searchPaths)
		if modulePath == "" {
			continue
		}
		alias := defaultImportAlias(modulePath)
		if alias == "" || seen[alias] {
			continue
		}
		if _, imported := file.imports[alias]; imported {
			continue
		}
		target, err := a.loadFile(candidate)
		if err != nil {
			continue
		}
		module := importableModule{
			Path:  modulePath,
			Alias: alias,
		}
		for name, sym := range target.topLevel {
			module.members = append(module.members, CompletionItem{
				Label:         name,
				Kind:          string(sym.kind),
				Detail:        sym.detail,
				Documentation: sym.doc,
			})
		}
		sort.Slice(module.members, func(i, j int) bool { return module.members[i].Label < module.members[j].Label })
		out = append(out, module)
		seen[alias] = true
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Alias != out[j].Alias {
			return out[i].Alias < out[j].Alias
		}
		return out[i].Path < out[j].Path
	})
	return out
}

func sameDocumentPath(a, b string) bool {
	aa, errA := filepath.Abs(a)
	bb, errB := filepath.Abs(b)
	if errA != nil || errB != nil {
		return filepath.Clean(a) == filepath.Clean(b)
	}
	return filepath.Clean(aa) == filepath.Clean(bb)
}

func existingImportPaths(prog *ast.Program) map[string]bool {
	out := make(map[string]bool)
	if prog == nil {
		return out
	}
	for _, decl := range prog.Decls {
		imp, ok := decl.(*ast.ImportDecl)
		if !ok {
			continue
		}
		out[imp.Path] = true
	}
	return out
}

func sourceWithAddedImport(src string, modulePath string) (string, error) {
	tokensWithComments, err := lexer.New(src).TokenizeWithComments()
	if err != nil {
		return "", err
	}
	tokens, err := lexer.New(src).Tokenize()
	if err != nil {
		return "", err
	}
	prog, err := parser.New(tokens).Parse()
	if err != nil {
		return "", err
	}
	for _, decl := range prog.Decls {
		if imp, ok := decl.(*ast.ImportDecl); ok && imp.Path == modulePath {
			return src, nil
		}
	}
	line := 1
	for _, decl := range prog.Decls {
		if imp, ok := decl.(*ast.ImportDecl); ok && imp.Line > 0 {
			line = imp.Line
			break
		}
	}
	prog.Decls = append(prog.Decls, &ast.ImportDecl{Path: modulePath, Line: line})
	return string(formatter.Format(prog, tokensWithComments)), nil
}

func insertImportEdit(path string, src string, modulePath string) (RenameEdit, error) {
	tokens, err := lexer.New(src).Tokenize()
	if err != nil {
		return RenameEdit{}, err
	}
	prog, err := parser.New(tokens).Parse()
	if err != nil {
		return RenameEdit{}, err
	}
	for _, decl := range prog.Decls {
		if imp, ok := decl.(*ast.ImportDecl); ok && imp.Path == modulePath {
			return RenameEdit{}, fmt.Errorf("import already exists")
		}
	}
	lines := strings.Split(src, "\n")
	lastImportLine := 0
	for _, decl := range prog.Decls {
		imp, ok := decl.(*ast.ImportDecl)
		if !ok {
			continue
		}
		if imp.Line > lastImportLine {
			lastImportLine = imp.Line
		}
	}
	if lastImportLine > 0 {
		lineText := ""
		if lastImportLine-1 < len(lines) {
			lineText = lines[lastImportLine-1]
		}
		col := len(lineText) + 1
		return RenameEdit{
			Path:    path,
			Line:    lastImportLine,
			Col:     col,
			EndCol:  col - 1,
			NewText: "\nimport \"" + modulePath + "\";",
		}, nil
	}
	return RenameEdit{
		Path:    path,
		Line:    1,
		Col:     1,
		EndCol:  0,
		NewText: "import \"" + modulePath + "\";\n\n",
	}, nil
}

func moduleImportPath(path string, searchPaths []string) string {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return ""
	}
	for _, root := range searchPaths {
		absRoot, err := filepath.Abs(root)
		if err != nil {
			continue
		}
		rel, err := filepath.Rel(absRoot, absPath)
		if err != nil || rel == "." || strings.HasPrefix(rel, ".."+string(filepath.Separator)) || rel == ".." {
			continue
		}
		rel = filepath.ToSlash(rel)
		if !strings.HasSuffix(rel, ".basl") {
			continue
		}
		base := strings.TrimSuffix(rel, ".basl")
		if !strings.Contains(base, "/") {
			if base == "main" {
				return ""
			}
			return base
		}
		parts := strings.Split(base, "/")
		if len(parts) == 3 && parts[1] == "lib" && parts[0] == parts[2] {
			return parts[0]
		}
	}
	return ""
}

func defaultImportAlias(modulePath string) string {
	modulePath = strings.TrimSpace(modulePath)
	if modulePath == "" {
		return ""
	}
	parts := strings.Split(modulePath, "/")
	return parts[len(parts)-1]
}

func (m importableModule) memberCompletions() []CompletionItem {
	out := make([]CompletionItem, len(m.members))
	copy(out, m.members)
	return out
}

func (m importableModule) memberByName(name string) (CompletionItem, bool) {
	for _, item := range m.members {
		if item.Label == name {
			return item, true
		}
	}
	return CompletionItem{}, false
}

func findUnknownIdentifierRange(src string, diag DiagnosticHint, targetRange *Location, name string) *Location {
	if targetRange != nil && targetRange.Line > 0 {
		if loc := identifierRangeOnLine(src, targetRange.Line, name, targetRange.Col); loc != nil {
			return loc
		}
	}
	if diag.Range != nil && diag.Range.Line > 0 {
		if loc := identifierRangeOnLine(src, diag.Range.Line, name, diag.Range.Col); loc != nil {
			return loc
		}
	}
	return nil
}

func identifierRangeOnLine(src string, line int, name string, nearCol int) *Location {
	lines := strings.Split(src, "\n")
	if line <= 0 || line > len(lines) {
		return nil
	}
	text := lines[line-1]
	bestIdx := -1
	for idx := 0; idx <= len(text)-len(name); idx++ {
		if text[idx:idx+len(name)] != name {
			continue
		}
		if idx > 0 {
			prev := text[idx-1]
			if isIdentifierByte(prev) {
				continue
			}
		}
		if idx+len(name) < len(text) {
			next := text[idx+len(name)]
			if isIdentifierByte(next) {
				continue
			}
		}
		if bestIdx < 0 {
			bestIdx = idx
		}
		if nearCol > 0 && idx+1 >= nearCol {
			bestIdx = idx
			break
		}
	}
	if bestIdx < 0 {
		return nil
	}
	return &Location{
		Line:   line,
		Col:    bestIdx + 1,
		EndCol: bestIdx + len(name),
	}
}

func isIdentifierByte(ch byte) bool {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'
}

func wantsCodeActionKind(kind string, only []string) bool {
	if len(only) == 0 {
		return true
	}
	for _, item := range only {
		if item == kind || strings.HasPrefix(kind, item+".") || strings.HasPrefix(item, kind+".") {
			return true
		}
	}
	return false
}

func dedupeCodeActions(items []CodeAction) []CodeAction {
	seen := make(map[string]bool)
	var out []CodeAction
	for _, item := range items {
		key := item.Kind + ":" + item.Title
		if seen[key] {
			continue
		}
		seen[key] = true
		out = append(out, item)
	}
	return out
}

func importFoldingRanges(src string) []FoldingRange {
	tokens, err := lexer.New(src).Tokenize()
	if err != nil {
		return nil
	}
	prog, err := parser.New(tokens).Parse()
	if err != nil || prog == nil {
		return nil
	}
	var importLines []int
	for _, decl := range prog.Decls {
		imp, ok := decl.(*ast.ImportDecl)
		if !ok {
			continue
		}
		if imp.Line > 0 {
			importLines = append(importLines, imp.Line)
		}
	}
	if len(importLines) < 2 {
		return nil
	}
	sort.Ints(importLines)
	var out []FoldingRange
	start := importLines[0]
	prev := importLines[0]
	for _, line := range importLines[1:] {
		if line == prev+1 {
			prev = line
			continue
		}
		if prev > start {
			out = append(out, FoldingRange{StartLine: start, EndLine: prev, Kind: "imports"})
		}
		start = line
		prev = line
	}
	if prev > start {
		out = append(out, FoldingRange{StartLine: start, EndLine: prev, Kind: "imports"})
	}
	return out
}

func commentFoldingRanges(tokens []lexer.Token) []FoldingRange {
	var out []FoldingRange
	lineCommentStart := 0
	lineCommentEnd := 0
	flushLineComments := func() {
		if lineCommentStart > 0 && lineCommentEnd > lineCommentStart {
			out = append(out, FoldingRange{StartLine: lineCommentStart, EndLine: lineCommentEnd, Kind: "comment"})
		}
		lineCommentStart = 0
		lineCommentEnd = 0
	}
	for _, tok := range tokens {
		switch tok.Type {
		case lexer.TOKEN_LINE_COMMENT:
			if lineCommentStart == 0 {
				lineCommentStart = tok.Line
				lineCommentEnd = tok.Line
				continue
			}
			if tok.Line == lineCommentEnd+1 {
				lineCommentEnd = tok.Line
				continue
			}
			flushLineComments()
			lineCommentStart = tok.Line
			lineCommentEnd = tok.Line
		case lexer.TOKEN_BLOCK_COMMENT:
			flushLineComments()
			endLine := tok.Line + strings.Count(tok.Literal, "\n")
			if endLine > tok.Line {
				out = append(out, FoldingRange{StartLine: tok.Line, EndLine: endLine, Kind: "comment"})
			}
		default:
			flushLineComments()
		}
	}
	flushLineComments()
	return out
}

func braceFoldingRanges(tokens []lexer.Token) []FoldingRange {
	type openToken struct {
		kind string
		line int
	}
	var stack []openToken
	var out []FoldingRange
	for _, tok := range tokens {
		switch tok.Type {
		case lexer.TOKEN_LBRACE:
			stack = append(stack, openToken{kind: "region", line: tok.Line})
		case lexer.TOKEN_LBRACKET:
			stack = append(stack, openToken{kind: "region", line: tok.Line})
		case lexer.TOKEN_RBRACE, lexer.TOKEN_RBRACKET:
			if len(stack) == 0 {
				continue
			}
			open := stack[len(stack)-1]
			stack = stack[:len(stack)-1]
			endLine := tok.Line - 1
			if endLine > open.line {
				out = append(out, FoldingRange{StartLine: open.line, EndLine: endLine})
			}
		}
	}
	return out
}

func dedupeFoldingRanges(items []FoldingRange) []FoldingRange {
	seen := make(map[string]bool)
	var out []FoldingRange
	for _, item := range items {
		if item.StartLine <= 0 || item.EndLine <= item.StartLine {
			continue
		}
		key := fmt.Sprintf("%d:%d:%s", item.StartLine, item.EndLine, item.Kind)
		if seen[key] {
			continue
		}
		seen[key] = true
		out = append(out, item)
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].StartLine != out[j].StartLine {
			return out[i].StartLine < out[j].StartLine
		}
		if out[i].EndLine != out[j].EndLine {
			return out[i].EndLine < out[j].EndLine
		}
		return out[i].Kind < out[j].Kind
	})
	return out
}
