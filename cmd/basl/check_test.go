package main

import (
	"io"
	"os"
	"strings"
	"testing"
)

func TestRunCheckReportsDiagnostics(t *testing.T) {
	dir := t.TempDir()
	path := writeDocLikeTestFile(t, dir, "main.basl", `
fn main() -> i32 {
    string s = "oops";
    return s;
}
`)

	code, stderr := captureCheck(t, []string{path})
	if code != 1 {
		t.Fatalf("runCheck() code = %d, want 1", code)
	}
	if !strings.Contains(stderr, "return value 1 expects i32, received string") {
		t.Fatalf("unexpected stderr:\n%s", stderr)
	}
}

func TestRunCheckSucceedsOnValidFile(t *testing.T) {
	dir := t.TempDir()
	path := writeDocLikeTestFile(t, dir, "main.basl", `
fn main() -> i32 {
    return 0;
}
`)

	code, stderr := captureCheck(t, []string{path})
	if code != 0 {
		t.Fatalf("runCheck() code = %d, want 0; stderr:\n%s", code, stderr)
	}
	if stderr != "" {
		t.Fatalf("expected empty stderr, got:\n%s", stderr)
	}
}

func captureCheck(t *testing.T, args []string) (int, string) {
	t.Helper()

	origStderr := os.Stderr
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatalf("os.Pipe() error = %v", err)
	}
	defer r.Close()

	os.Stderr = w
	code := runCheck(args)
	_ = w.Close()
	os.Stderr = origStderr

	out, err := io.ReadAll(r)
	if err != nil {
		t.Fatalf("io.ReadAll() error = %v", err)
	}
	return code, strings.TrimSpace(string(out))
}
