package lsp

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
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
import "helper";

fn main() -> void {
    helper.Greeter g = helper.Greeter();
    string value = helper.message();
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
			"position":     map[string]any{"line": 4, "character": 27},
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
			"position":     map[string]any{"line": 4, "character": 12},
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
		"id":      5,
		"method":  "shutdown",
	})
	msg = readOne(t, reader)
	assertID(t, msg, "5")
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
