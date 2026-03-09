package editor

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
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
