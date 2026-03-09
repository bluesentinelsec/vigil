package dap

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"testing"
	"time"
)

func TestServeLaunchesAndStopsOnEntry(t *testing.T) {
	root := t.TempDir()
	mainPath := filepath.Join(root, "main.basl")
	if err := os.WriteFile(mainPath, []byte(`import "fmt";

fn main() -> void {
    string name = "world";
    fmt.println(name);
}
`), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}

	serverConn, clientConn := net.Pipe()
	defer clientConn.Close()
	done := make(chan error, 1)
	go func() {
		done <- Serve(context.Background(), serverConn, serverConn)
	}()

	reader := bufio.NewReader(clientConn)
	sendRequest(t, clientConn, 1, "initialize", map[string]any{})
	msg := readProtocolMessage(t, reader)
	if msg.Type != "response" || !msg.Success || msg.Command != "initialize" {
		t.Fatalf("initialize response = %#v", msg)
	}

	sendRequest(t, clientConn, 2, "launch", map[string]any{
		"program":     mainPath,
		"stopOnEntry": true,
	})
	msg = readProtocolMessage(t, reader)
	if msg.Type != "response" || !msg.Success || msg.Command != "launch" {
		t.Fatalf("launch response = %#v", msg)
	}
	msg = readProtocolMessage(t, reader)
	if msg.Type != "event" || msg.Event != "initialized" {
		t.Fatalf("initialized event = %#v", msg)
	}

	sendRequest(t, clientConn, 3, "configurationDone", map[string]any{})
	msg = readProtocolMessage(t, reader)
	if msg.Type != "response" || !msg.Success || msg.Command != "configurationDone" {
		t.Fatalf("configurationDone response = %#v", msg)
	}
	msg = readProtocolMessage(t, reader)
	if msg.Type != "event" || msg.Event != "stopped" {
		t.Fatalf("stopped event = %#v", msg)
	}

	sendRequest(t, clientConn, 4, "stackTrace", map[string]any{"threadId": 1})
	msg = readProtocolMessage(t, reader)
	if msg.Type != "response" || !msg.Success || msg.Command != "stackTrace" {
		t.Fatalf("stackTrace response = %#v", msg)
	}
	var body struct {
		StackFrames []stackFrame `json:"stackFrames"`
	}
	decodeBody(t, msg.Body, &body)
	if len(body.StackFrames) == 0 {
		t.Fatal("stackTrace returned no frames")
	}

	sendRequest(t, clientConn, 5, "disconnect", map[string]any{})
	msg = readProtocolMessage(t, reader)
	if msg.Type != "response" || !msg.Success || msg.Command != "disconnect" {
		t.Fatalf("disconnect response = %#v", msg)
	}
	_ = clientConn.Close()

	select {
	case err := <-done:
		if err != nil && err != io.EOF && err != context.Canceled {
			t.Fatalf("Serve() error = %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("server did not exit")
	}
}

func sendRequest(t *testing.T, w io.Writer, seq int, command string, args map[string]any) {
	t.Helper()
	payload := message{
		Seq:       seq,
		Type:      "request",
		Command:   command,
		Arguments: mustJSON(t, args),
	}
	data, err := json.Marshal(payload)
	if err != nil {
		t.Fatalf("json.Marshal() error = %v", err)
	}
	header := []byte("Content-Length: " + itoa(len(data)) + "\r\n\r\n")
	if _, err := w.Write(append(header, data...)); err != nil {
		t.Fatalf("Write() error = %v", err)
	}
}

func readProtocolMessage(t *testing.T, r *bufio.Reader) message {
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

func decodeBody(t *testing.T, body any, out any) {
	t.Helper()
	data, err := json.Marshal(body)
	if err != nil {
		t.Fatalf("json.Marshal(body) error = %v", err)
	}
	if err := json.Unmarshal(data, out); err != nil {
		t.Fatalf("json.Unmarshal(body) error = %v", err)
	}
}

func mustJSON(t *testing.T, v any) json.RawMessage {
	t.Helper()
	data, err := json.Marshal(v)
	if err != nil {
		t.Fatalf("json.Marshal() error = %v", err)
	}
	return data
}

func itoa(v int) string {
	return strconv.Itoa(v)
}
