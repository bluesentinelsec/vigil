package lsp

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestServerLifecycleAndCoreRequests(t *testing.T) {
	root := t.TempDir()
	writeTestFile(t, root, "basl.toml", "name = \"demo\"\nversion = \"0.1.0\"\n")
	writeTestFile(t, filepath.Join(root, "lib"), "helper.basl", `
pub class Greeter {
    pub string prefix;

    fn init() -> void {
        self.prefix = "hi";
    }

    pub fn greet(string name) -> string {
        return self.prefix + name;
    }
}

pub fn message() -> string {
    Greeter g = Greeter();
    return g.greet("basl");
}
`)
	mainPath := writeTestFile(t, root, "main.basl", `
import "fmt";
import "helper";

fn main() -> void {
    helper.Greeter g = helper.Greeter();
    string value = helper.message();
    fmt.println(value);
    g.greet(value);
}
`)

	serverConn, clientConn := net.Pipe()
	defer clientConn.Close()
	done := make(chan error, 1)
	go func() {
		done <- Serve(context.Background(), serverConn, serverConn)
	}()

	reader := bufio.NewReader(clientConn)
	rootURI := pathToURI(root)
	mainURI := pathToURI(mainPath)
	mainText, err := os.ReadFile(mainPath)
	if err != nil {
		t.Fatalf("os.ReadFile(main) error = %v", err)
	}
	mainSrc := string(mainText)

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      1,
		"method":  "initialize",
		"params": map[string]any{
			"rootUri": rootURI,
			"workspaceFolders": []map[string]any{
				{"uri": rootURI, "name": "demo"},
			},
		},
	})
	msg := readOne(t, reader)
	assertID(t, msg, "1")

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"method":  "initialized",
		"params":  map[string]any{},
	})
	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"method":  "textDocument/didOpen",
		"params": map[string]any{
			"textDocument": map[string]any{
				"uri":     mainURI,
				"version": 1,
				"text":    string(mainText),
			},
		},
	})
	diag := readOne(t, reader)
	if diag.Method != "textDocument/publishDiagnostics" {
		t.Fatalf("first notification method = %q, want textDocument/publishDiagnostics", diag.Method)
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      2,
		"method":  "textDocument/definition",
		"params": map[string]any{
			"textDocument": map[string]any{"uri": mainURI},
			"position":     mustLSPPosition(t, mainSrc, "message();", false),
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "2")
	var def struct {
		URI   string   `json:"uri"`
		Range lspRange `json:"range"`
	}
	decodeResult(t, msg.Result, &def)
	if filepath.Base(mustURIPath(t, def.URI)) != "helper.basl" {
		t.Fatalf("definition uri = %s, want helper.basl", def.URI)
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      3,
		"method":  "textDocument/hover",
		"params": map[string]any{
			"textDocument": map[string]any{"uri": mainURI},
			"position":     mustLSPPosition(t, mainSrc, "g = helper", false),
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "3")
	var hover hoverResult
	decodeResult(t, msg.Result, &hover)
	if hover.Contents.Value == "" {
		t.Fatal("hover contents were empty")
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      31,
		"method":  "textDocument/documentHighlight",
		"params": map[string]any{
			"textDocument": map[string]any{"uri": mainURI},
			"position":     mustLSPPosition(t, mainSrc, "value = helper", false),
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "31")
	var highlights []documentHighlight
	decodeResult(t, msg.Result, &highlights)
	if len(highlights) != 3 {
		t.Fatalf("documentHighlight len = %d, want 3", len(highlights))
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      4,
		"method":  "textDocument/documentSymbol",
		"params": map[string]any{
			"textDocument": map[string]any{"uri": mainURI},
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "4")
	var symbols []documentSymbol
	decodeResult(t, msg.Result, &symbols)
	if len(symbols) == 0 {
		t.Fatal("documentSymbol returned no symbols")
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      41,
		"method":  "workspace/symbol",
		"params": map[string]any{
			"query": "greet",
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "41")
	var workspaceSymbols []symbolInformation
	decodeResult(t, msg.Result, &workspaceSymbols)
	if len(workspaceSymbols) == 0 {
		t.Fatal("workspace/symbol returned no symbols")
	}
	foundWorkspaceMethod := false
	for _, item := range workspaceSymbols {
		if item.Name == "greet" && item.ContainerName == "Greeter" && filepath.Base(mustURIPath(t, item.Location.URI)) == "helper.basl" {
			foundWorkspaceMethod = true
			break
		}
	}
	if !foundWorkspaceMethod {
		t.Fatalf("workspace symbols = %#v, want Greeter.greet from helper.basl", workspaceSymbols)
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      5,
		"method":  "textDocument/completion",
		"params": map[string]any{
			"textDocument": map[string]any{"uri": mainURI},
			"position":     mustLSPPosition(t, mainSrc, "fmt.", true),
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "5")
	var completions []completionItem
	decodeResult(t, msg.Result, &completions)
	foundBuiltin := false
	for _, item := range completions {
		if item.Label == "println" && item.Documentation != "" {
			foundBuiltin = true
			break
		}
	}
	if !foundBuiltin {
		t.Fatalf("completion docs = %#v, want builtin documentation", completions)
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      6,
		"method":  "textDocument/signatureHelp",
		"params": map[string]any{
			"textDocument": map[string]any{"uri": mainURI},
			"position":     mustLSPPosition(t, mainSrc, "fmt.println(", true),
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "6")
	var signatureHelp signatureHelpResult
	decodeResult(t, msg.Result, &signatureHelp)
	if len(signatureHelp.Signatures) == 0 || signatureHelp.Signatures[0].Label == "" {
		t.Fatalf("signatureHelp = %#v, want signature", signatureHelp)
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      7,
		"method":  "textDocument/prepareRename",
		"params": map[string]any{
			"textDocument": map[string]any{"uri": mainURI},
			"position":     mustLSPPosition(t, mainSrc, "value = helper", false),
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "7")
	var prepare prepareRenameResult
	decodeResult(t, msg.Result, &prepare)
	if prepare.Placeholder != "value" {
		t.Fatalf("prepareRename = %#v, want placeholder value", prepare)
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      8,
		"method":  "textDocument/formatting",
		"params": map[string]any{
			"textDocument": map[string]any{"uri": mainURI},
			"options":      map[string]any{"insertSpaces": true, "tabSize": 4},
		},
	})
	msg = readOne(t, reader)
	assertID(t, msg, "8")
	var edits []textEdit
	decodeResult(t, msg.Result, &edits)
	if edits != nil && len(edits) != 0 {
		t.Fatalf("formatting edits = %#v, want no-op on formatted file", edits)
	}

	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"id":      9,
		"method":  "shutdown",
	})
	msg = readOne(t, reader)
	assertID(t, msg, "9")
	send(t, clientConn, map[string]any{
		"jsonrpc": "2.0",
		"method":  "exit",
	})

	select {
	case err := <-done:
		if err != nil && err != io.EOF && err != context.Canceled {
			t.Fatalf("Serve() error = %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("server did not exit")
	}
}

func TestFileURIPathRoundTrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), "main.basl")
	uri := pathToURI(path)
	got, ok := uriToPath(uri)
	if !ok {
		t.Fatalf("uriToPath(%q) failed", uri)
	}
	if filepath.Clean(got) != filepath.Clean(path) {
		t.Fatalf("roundtrip path = %q, want %q (uri=%q)", got, path, uri)
	}
}

func send(t *testing.T, w io.Writer, msg map[string]any) {
	t.Helper()
	if err := writeMessage(w, mustMessage(t, msg)); err != nil {
		t.Fatalf("writeMessage() error = %v", err)
	}
}

func readOne(t *testing.T, r *bufio.Reader) message {
	t.Helper()
	body, err := readMessage(r)
	if err != nil {
		t.Fatalf("readMessage() error = %v", err)
	}
	var msg message
	if err := json.Unmarshal(body, &msg); err != nil {
		t.Fatalf("json.Unmarshal() error = %v", err)
	}
	return msg
}

func mustMessage(t *testing.T, in map[string]any) message {
	t.Helper()
	data, err := json.Marshal(in)
	if err != nil {
		t.Fatalf("json.Marshal() error = %v", err)
	}
	var msg message
	if err := json.Unmarshal(data, &msg); err != nil {
		t.Fatalf("json.Unmarshal() error = %v", err)
	}
	return msg
}

func assertID(t *testing.T, msg message, want string) {
	t.Helper()
	if string(msg.ID) != want {
		t.Fatalf("response id = %s, want %s", msg.ID, want)
	}
}

func decodeResult(t *testing.T, result any, out any) {
	t.Helper()
	data, err := json.Marshal(result)
	if err != nil {
		t.Fatalf("json.Marshal(result) error = %v", err)
	}
	if err := json.Unmarshal(data, out); err != nil {
		t.Fatalf("json.Unmarshal(result) error = %v", err)
	}
}

func mustLSPPosition(t *testing.T, src string, needle string, after bool) map[string]any {
	t.Helper()
	idx := strings.Index(src, needle)
	if idx < 0 {
		t.Fatalf("needle %q not found", needle)
	}
	if after {
		idx += len(needle)
	}
	line := 0
	character := 0
	for i, r := range src {
		if i >= idx {
			break
		}
		if r == '\n' {
			line++
			character = 0
		} else {
			character++
		}
	}
	return map[string]any{"line": line, "character": character}
}

func mustURIPath(t *testing.T, uri string) string {
	t.Helper()
	path, ok := uriToPath(uri)
	if !ok {
		t.Fatalf("uriToPath(%q) failed", uri)
	}
	return path
}

func writeTestFile(t *testing.T, root, relPath, contents string) string {
	t.Helper()
	path := filepath.Join(root, relPath)
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatalf("os.MkdirAll() error = %v", err)
	}
	if err := os.WriteFile(path, []byte(trimLeadingNewline(contents)), 0644); err != nil {
		t.Fatalf("os.WriteFile() error = %v", err)
	}
	return path
}

func trimLeadingNewline(s string) string {
	if len(s) > 0 && s[0] == '\n' {
		return s[1:]
	}
	return s
}
