package lsp

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"

	basleditor "github.com/bluesentinelsec/basl/pkg/basl/editor"
)

type responseError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

type message struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"`
	Params  json.RawMessage `json:"params,omitempty"`
	Result  any             `json:"result,omitempty"`
	Error   *responseError  `json:"error,omitempty"`
}

type Server struct {
	mu             sync.Mutex
	sender         func(message) error
	workspaceRoots []string
	documents      map[string]*document
	shutdown       bool
}

type document struct {
	URI     string
	Path    string
	Text    string
	Version int
}

type initializeParams struct {
	WorkspaceFolders []workspaceFolder `json:"workspaceFolders"`
	RootURI          string            `json:"rootUri"`
	RootPath         string            `json:"rootPath"`
}

type workspaceFolder struct {
	URI  string `json:"uri"`
	Name string `json:"name"`
}

type didOpenParams struct {
	TextDocument textDocumentItem `json:"textDocument"`
}

type textDocumentItem struct {
	URI     string `json:"uri"`
	Text    string `json:"text"`
	Version int    `json:"version"`
}

type didChangeParams struct {
	TextDocument   versionedTextDocumentIdentifier  `json:"textDocument"`
	ContentChanges []textDocumentContentChangeEvent `json:"contentChanges"`
}

type versionedTextDocumentIdentifier struct {
	URI     string `json:"uri"`
	Version int    `json:"version"`
}

type textDocumentContentChangeEvent struct {
	Text string `json:"text"`
}

type didCloseParams struct {
	TextDocument textDocumentIdentifier `json:"textDocument"`
}

type didSaveParams struct {
	TextDocument textDocumentIdentifier `json:"textDocument"`
}

type textDocumentIdentifier struct {
	URI string `json:"uri"`
}

type textDocumentPositionParams struct {
	TextDocument textDocumentIdentifier `json:"textDocument"`
	Position     position               `json:"position"`
}

type position struct {
	Line      int `json:"line"`
	Character int `json:"character"`
}

type renameParams struct {
	TextDocument textDocumentIdentifier `json:"textDocument"`
	Position     position               `json:"position"`
	NewName      string                 `json:"newName"`
}

type publishDiagnosticsParams struct {
	URI         string           `json:"uri"`
	Diagnostics []diagnosticItem `json:"diagnostics"`
}

type diagnosticItem struct {
	Range    lspRange `json:"range"`
	Severity int      `json:"severity"`
	Message  string   `json:"message"`
	Source   string   `json:"source,omitempty"`
}

type lspLocation struct {
	URI   string   `json:"uri"`
	Range lspRange `json:"range"`
}

type lspRange struct {
	Start position `json:"start"`
	End   position `json:"end"`
}

type markupContent struct {
	Kind  string `json:"kind"`
	Value string `json:"value"`
}

type hoverResult struct {
	Contents markupContent `json:"contents"`
	Range    *lspRange     `json:"range,omitempty"`
}

type completionItem struct {
	Label         string `json:"label"`
	Kind          int    `json:"kind,omitempty"`
	Detail        string `json:"detail,omitempty"`
	Documentation string `json:"documentation,omitempty"`
}

type parameterInformation struct {
	Label         string `json:"label"`
	Documentation string `json:"documentation,omitempty"`
}

type signatureInformation struct {
	Label         string                 `json:"label"`
	Documentation string                 `json:"documentation,omitempty"`
	Parameters    []parameterInformation `json:"parameters,omitempty"`
}

type signatureHelpResult struct {
	Signatures      []signatureInformation `json:"signatures"`
	ActiveSignature int                    `json:"activeSignature"`
	ActiveParameter int                    `json:"activeParameter"`
}

type textEdit struct {
	Range   lspRange `json:"range"`
	NewText string   `json:"newText"`
}

type workspaceEdit struct {
	Changes map[string][]textEdit `json:"changes"`
}

type prepareRenameResult struct {
	Range       lspRange `json:"range"`
	Placeholder string   `json:"placeholder,omitempty"`
}

type documentSymbol struct {
	Name           string           `json:"name"`
	Detail         string           `json:"detail,omitempty"`
	Kind           int              `json:"kind"`
	Range          lspRange         `json:"range"`
	SelectionRange lspRange         `json:"selectionRange"`
	Children       []documentSymbol `json:"children,omitempty"`
}

func NewServer(sender func(message) error) *Server {
	return &Server{
		sender:    sender,
		documents: make(map[string]*document),
	}
}

func (s *Server) Serve(ctx context.Context, r io.Reader) error {
	br := bufio.NewReader(r)
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}

		body, err := readMessage(br)
		if err != nil {
			if err == io.EOF {
				return nil
			}
			return err
		}
		var msg message
		if err := json.Unmarshal(body, &msg); err != nil {
			return err
		}
		if err := s.handleMessage(msg); err != nil {
			return err
		}
	}
}

func (s *Server) handleMessage(msg message) error {
	switch msg.Method {
	case "initialize":
		return s.handleInitialize(msg)
	case "initialized":
		return nil
	case "shutdown":
		s.mu.Lock()
		s.shutdown = true
		s.mu.Unlock()
		return s.reply(msg.ID, map[string]any{})
	case "exit":
		return io.EOF
	case "textDocument/didOpen":
		return s.handleDidOpen(msg.Params)
	case "textDocument/didChange":
		return s.handleDidChange(msg.Params)
	case "textDocument/didSave":
		return s.handleDidSave(msg.Params)
	case "textDocument/didClose":
		return s.handleDidClose(msg.Params)
	case "textDocument/definition":
		return s.handleDefinition(msg)
	case "textDocument/hover":
		return s.handleHover(msg)
	case "textDocument/references":
		return s.handleReferences(msg)
	case "textDocument/rename":
		return s.handleRename(msg)
	case "textDocument/prepareRename":
		return s.handlePrepareRename(msg)
	case "textDocument/completion":
		return s.handleCompletion(msg)
	case "textDocument/signatureHelp":
		return s.handleSignatureHelp(msg)
	case "textDocument/documentSymbol":
		return s.handleDocumentSymbol(msg)
	case "textDocument/formatting":
		return s.handleFormatting(msg)
	default:
		if len(msg.ID) > 0 {
			return s.replyError(msg.ID, -32601, "method not found")
		}
		return nil
	}
}

func (s *Server) handleInitialize(msg message) error {
	var params initializeParams
	_ = json.Unmarshal(msg.Params, &params)
	s.mu.Lock()
	s.workspaceRoots = nil
	for _, folder := range params.WorkspaceFolders {
		if path, ok := uriToPath(folder.URI); ok {
			s.workspaceRoots = append(s.workspaceRoots, path)
		}
	}
	if len(s.workspaceRoots) == 0 {
		if path, ok := uriToPath(params.RootURI); ok {
			s.workspaceRoots = append(s.workspaceRoots, path)
		} else if params.RootPath != "" {
			s.workspaceRoots = append(s.workspaceRoots, params.RootPath)
		}
	}
	s.mu.Unlock()

	result := map[string]any{
		"capabilities": map[string]any{
			"textDocumentSync":           1,
			"definitionProvider":         true,
			"hoverProvider":              true,
			"referencesProvider":         true,
			"renameProvider":             map[string]any{"prepareProvider": true},
			"documentSymbolProvider":     true,
			"documentFormattingProvider": true,
			"completionProvider": map[string]any{
				"triggerCharacters": []string{"."},
			},
			"signatureHelpProvider": map[string]any{
				"triggerCharacters": []string{"(", ","},
			},
		},
		"serverInfo": map[string]any{
			"name": "basl",
		},
	}
	return s.reply(msg.ID, result)
}

func (s *Server) handleDidOpen(raw json.RawMessage) error {
	var params didOpenParams
	if err := json.Unmarshal(raw, &params); err != nil {
		return err
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return nil
	}
	s.mu.Lock()
	s.documents[params.TextDocument.URI] = &document{
		URI:     params.TextDocument.URI,
		Path:    path,
		Text:    params.TextDocument.Text,
		Version: params.TextDocument.Version,
	}
	s.mu.Unlock()
	return s.publishDiagnosticsForURI(params.TextDocument.URI)
}

func (s *Server) handleDidChange(raw json.RawMessage) error {
	var params didChangeParams
	if err := json.Unmarshal(raw, &params); err != nil {
		return err
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return nil
	}
	s.mu.Lock()
	doc := s.documents[params.TextDocument.URI]
	if doc == nil {
		doc = &document{URI: params.TextDocument.URI, Path: path}
		s.documents[params.TextDocument.URI] = doc
	}
	if len(params.ContentChanges) > 0 {
		doc.Text = params.ContentChanges[len(params.ContentChanges)-1].Text
	}
	doc.Version = params.TextDocument.Version
	s.mu.Unlock()
	return s.publishDiagnosticsForURI(params.TextDocument.URI)
}

func (s *Server) handleDidSave(raw json.RawMessage) error {
	var params didSaveParams
	if err := json.Unmarshal(raw, &params); err != nil {
		return err
	}
	return s.publishDiagnosticsForURI(params.TextDocument.URI)
}

func (s *Server) handleDidClose(raw json.RawMessage) error {
	var params didCloseParams
	if err := json.Unmarshal(raw, &params); err != nil {
		return err
	}
	s.mu.Lock()
	delete(s.documents, params.TextDocument.URI)
	s.mu.Unlock()
	return s.notify("textDocument/publishDiagnostics", publishDiagnosticsParams{
		URI:         params.TextDocument.URI,
		Diagnostics: []diagnosticItem{},
	})
}

func (s *Server) handleDefinition(msg message) error {
	var params textDocumentPositionParams
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, nil)
	}
	loc, err := basleditor.DefinitionWithOptions(path, toEditorPosition(params.Position), s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	if loc == nil {
		return s.reply(msg.ID, nil)
	}
	return s.reply(msg.ID, toLSPLocation(*loc))
}

func (s *Server) handleHover(msg message) error {
	var params textDocumentPositionParams
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, nil)
	}
	hover, err := basleditor.HoverAtWithOptions(path, toEditorPosition(params.Position), s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	if hover == nil {
		return s.reply(msg.ID, nil)
	}
	out := hoverResult{
		Contents: markupContent{
			Kind:  "markdown",
			Value: "```basl\n" + hover.Contents + "\n```",
		},
	}
	if hover.Location != nil {
		r := toLSPRange(*hover.Location)
		out.Range = &r
	}
	return s.reply(msg.ID, out)
}

func (s *Server) handleReferences(msg message) error {
	var params textDocumentPositionParams
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, []lspLocation{})
	}
	refs, err := basleditor.ReferencesWithOptions(path, toEditorPosition(params.Position), s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	out := make([]lspLocation, 0, len(refs))
	for _, item := range refs {
		out = append(out, toLSPLocation(item))
	}
	return s.reply(msg.ID, out)
}

func (s *Server) handleRename(msg message) error {
	var params renameParams
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, nil)
	}
	edits, err := basleditor.RenameWithOptions(path, toEditorPosition(params.Position), params.NewName, s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	if len(edits) == 0 {
		return s.reply(msg.ID, nil)
	}
	out := workspaceEdit{Changes: make(map[string][]textEdit)}
	for _, edit := range edits {
		uri := pathToURI(edit.Path)
		out.Changes[uri] = append(out.Changes[uri], textEdit{
			Range:   toLSPRange(basleditor.Location{Path: edit.Path, Line: edit.Line, Col: edit.Col, EndCol: edit.EndCol}),
			NewText: edit.NewText,
		})
	}
	return s.reply(msg.ID, out)
}

func (s *Server) handlePrepareRename(msg message) error {
	var params textDocumentPositionParams
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, nil)
	}
	item, err := basleditor.PrepareRenameAtWithOptions(path, toEditorPosition(params.Position), s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	if item == nil {
		return s.reply(msg.ID, nil)
	}
	return s.reply(msg.ID, prepareRenameResult{
		Range:       toLSPRange(item.Range),
		Placeholder: item.Placeholder,
	})
}

func (s *Server) handleCompletion(msg message) error {
	var params textDocumentPositionParams
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, []completionItem{})
	}
	items, err := basleditor.CompletionsWithOptions(path, toEditorPosition(params.Position), s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	out := make([]completionItem, 0, len(items))
	for _, item := range items {
		out = append(out, completionItem{
			Label:         item.Label,
			Kind:          completionKind(item.Kind),
			Detail:        item.Detail,
			Documentation: item.Documentation,
		})
	}
	return s.reply(msg.ID, out)
}

func (s *Server) handleSignatureHelp(msg message) error {
	var params textDocumentPositionParams
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, nil)
	}
	help, err := basleditor.SignatureHelpAtWithOptions(path, toEditorPosition(params.Position), s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	if help == nil {
		return s.reply(msg.ID, nil)
	}
	out := signatureHelpResult{
		Signatures:      make([]signatureInformation, 0, len(help.Signatures)),
		ActiveSignature: help.ActiveSignature,
		ActiveParameter: help.ActiveParameter,
	}
	for _, sig := range help.Signatures {
		item := signatureInformation{
			Label:         sig.Label,
			Documentation: sig.Documentation,
			Parameters:    make([]parameterInformation, 0, len(sig.Parameters)),
		}
		for _, param := range sig.Parameters {
			item.Parameters = append(item.Parameters, parameterInformation{
				Label:         param.Label,
				Documentation: param.Documentation,
			})
		}
		out.Signatures = append(out.Signatures, item)
	}
	return s.reply(msg.ID, out)
}

func (s *Server) handleDocumentSymbol(msg message) error {
	var params struct {
		TextDocument textDocumentIdentifier `json:"textDocument"`
	}
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, []documentSymbol{})
	}
	items, err := basleditor.DocumentSymbolsWithOptions(path, s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	out := make([]documentSymbol, 0, len(items))
	for _, item := range items {
		out = append(out, toDocumentSymbol(item))
	}
	return s.reply(msg.ID, out)
}

func (s *Server) handleFormatting(msg message) error {
	var params struct {
		TextDocument textDocumentIdentifier `json:"textDocument"`
	}
	if err := json.Unmarshal(msg.Params, &params); err != nil {
		return s.replyError(msg.ID, -32602, err.Error())
	}
	path, ok := uriToPath(params.TextDocument.URI)
	if !ok {
		return s.reply(msg.ID, []textEdit{})
	}
	formatted, err := basleditor.FormatDocumentWithOptions(path, s.editorOptions())
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	original, err := s.documentTextForURI(params.TextDocument.URI)
	if err != nil {
		return s.replyError(msg.ID, -32603, err.Error())
	}
	if original == formatted {
		return s.reply(msg.ID, []textEdit{})
	}
	return s.reply(msg.ID, []textEdit{{
		Range:   fullDocumentRange(original),
		NewText: formatted,
	}})
}

func (s *Server) editorOptions() basleditor.Options {
	s.mu.Lock()
	defer s.mu.Unlock()
	overlays := make(map[string]string, len(s.documents))
	for _, doc := range s.documents {
		overlays[doc.Path] = doc.Text
	}
	return basleditor.Options{
		SearchPaths: append([]string(nil), s.workspaceRoots...),
		Overlays:    overlays,
	}
}

func (s *Server) documentTextForURI(uri string) (string, error) {
	path, ok := uriToPath(uri)
	if !ok {
		return "", fmt.Errorf("invalid document URI")
	}
	s.mu.Lock()
	if doc := s.documents[uri]; doc != nil {
		text := doc.Text
		s.mu.Unlock()
		return text, nil
	}
	s.mu.Unlock()
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return string(data), nil
}

func (s *Server) publishDiagnosticsForURI(uri string) error {
	path, ok := uriToPath(uri)
	if !ok {
		return nil
	}
	diags, err := basleditor.DiagnosticsWithOptions(path, s.editorOptions())
	if err != nil {
		return s.notify("textDocument/publishDiagnostics", publishDiagnosticsParams{
			URI: uri,
			Diagnostics: []diagnosticItem{{
				Range: lspRange{
					Start: position{Line: 0, Character: 0},
					End:   position{Line: 0, Character: 1},
				},
				Severity: 1,
				Message:  err.Error(),
				Source:   "basl",
			}},
		})
	}
	out := make([]diagnosticItem, 0, len(diags))
	for _, item := range diags {
		if item.Path != "" && samePath(item.Path, path) == false {
			continue
		}
		startLine := max(item.Line-1, 0)
		startCol := max(item.Col-1, 0)
		endCol := startCol + 1
		out = append(out, diagnosticItem{
			Range: lspRange{
				Start: position{Line: startLine, Character: startCol},
				End:   position{Line: startLine, Character: endCol},
			},
			Severity: 1,
			Message:  item.Message,
			Source:   "basl",
		})
	}
	return s.notify("textDocument/publishDiagnostics", publishDiagnosticsParams{
		URI:         uri,
		Diagnostics: out,
	})
}

func (s *Server) reply(id json.RawMessage, result any) error {
	return s.sender(message{JSONRPC: "2.0", ID: id, Result: result})
}

func (s *Server) replyError(id json.RawMessage, code int, msg string) error {
	return s.sender(message{JSONRPC: "2.0", ID: id, Error: &responseError{Code: code, Message: msg}})
}

func (s *Server) notify(method string, params any) error {
	raw, err := json.Marshal(params)
	if err != nil {
		return err
	}
	return s.sender(message{JSONRPC: "2.0", Method: method, Params: raw})
}

func Serve(ctx context.Context, r io.Reader, w io.Writer) error {
	var mu sync.Mutex
	server := NewServer(func(msg message) error {
		mu.Lock()
		defer mu.Unlock()
		return writeMessage(w, msg)
	})
	return server.Serve(ctx, r)
}

func readMessage(r *bufio.Reader) ([]byte, error) {
	length := 0
	for {
		line, err := r.ReadString('\n')
		if err != nil {
			return nil, err
		}
		line = strings.TrimRight(line, "\r\n")
		if line == "" {
			break
		}
		if strings.HasPrefix(strings.ToLower(line), "content-length:") {
			value := strings.TrimSpace(strings.TrimPrefix(line, "Content-Length:"))
			value = strings.TrimSpace(strings.TrimPrefix(value, "content-length:"))
			n, err := strconv.Atoi(value)
			if err != nil {
				return nil, err
			}
			length = n
		}
	}
	if length <= 0 {
		return nil, fmt.Errorf("missing content length")
	}
	body := make([]byte, length)
	if _, err := io.ReadFull(r, body); err != nil {
		return nil, err
	}
	return body, nil
}

func writeMessage(w io.Writer, msg message) error {
	body, err := json.Marshal(msg)
	if err != nil {
		return err
	}
	header := fmt.Sprintf("Content-Length: %d\r\n\r\n", len(body))
	if _, err := io.Copy(w, bytes.NewBufferString(header)); err != nil {
		return err
	}
	_, err = w.Write(body)
	return err
}

func uriToPath(uri string) (string, bool) {
	u, err := url.Parse(uri)
	if err != nil {
		return "", false
	}
	if u.Scheme != "file" {
		return "", false
	}
	path := u.Path
	if path == "" {
		return "", false
	}
	if decoded, err := url.PathUnescape(path); err == nil {
		path = decoded
	}
	if runtimeGOOSWindows() && strings.HasPrefix(path, "/") && len(path) > 2 && path[2] == ':' {
		path = strings.TrimPrefix(path, "/")
	}
	return filepath.Clean(path), true
}

func pathToURI(path string) string {
	absPath, err := filepath.Abs(path)
	if err != nil {
		absPath = path
	}
	u := url.URL{Scheme: "file", Path: filepath.ToSlash(absPath)}
	return u.String()
}

func toEditorPosition(pos position) basleditor.Position {
	return basleditor.Position{Line: pos.Line + 1, Col: pos.Character + 1}
}

func toLSPLocation(loc basleditor.Location) lspLocation {
	return lspLocation{
		URI:   pathToURI(loc.Path),
		Range: toLSPRange(loc),
	}
}

func toLSPRange(loc basleditor.Location) lspRange {
	endCharacter := loc.EndCol
	if endCharacter < loc.Col {
		endCharacter = loc.Col
	}
	return lspRange{
		Start: position{Line: max(loc.Line-1, 0), Character: max(loc.Col-1, 0)},
		End:   position{Line: max(loc.Line-1, 0), Character: max(endCharacter, 1)},
	}
}

func toDocumentSymbol(item basleditor.SymbolItem) documentSymbol {
	sel := toLSPRange(item.Location)
	out := documentSymbol{
		Name:           item.Name,
		Detail:         item.Detail,
		Kind:           symbolKind(item.Kind),
		Range:          sel,
		SelectionRange: sel,
	}
	for _, child := range item.Children {
		out.Children = append(out.Children, toDocumentSymbol(child))
	}
	return out
}

func completionKind(kind string) int {
	switch kind {
	case "class":
		return 7
	case "method":
		return 2
	case "field":
		return 5
	case "variable", "parameter":
		return 6
	case "constant":
		return 21
	case "module", "import":
		return 9
	case "interface":
		return 8
	default:
		return 3
	}
}

func symbolKind(kind string) int {
	switch kind {
	case "class":
		return 5
	case "method":
		return 6
	case "field":
		return 8
	case "variable", "parameter":
		return 13
	case "constant":
		return 14
	case "interface":
		return 11
	case "enum":
		return 10
	default:
		return 12
	}
}

func samePath(a, b string) bool {
	aa, errA := filepath.Abs(a)
	bb, errB := filepath.Abs(b)
	if errA != nil || errB != nil {
		return a == b
	}
	return aa == bb
}

func max(v, min int) int {
	if v < min {
		return min
	}
	return v
}

func fullDocumentRange(text string) lspRange {
	lines := strings.Split(text, "\n")
	lastLine := 0
	lastChar := 0
	if len(lines) > 0 {
		lastLine = len(lines) - 1
		lastChar = len(lines[len(lines)-1])
	}
	return lspRange{
		Start: position{Line: 0, Character: 0},
		End:   position{Line: lastLine, Character: lastChar},
	}
}

func runtimeGOOSWindows() bool {
	return os.PathSeparator == '\\'
}
