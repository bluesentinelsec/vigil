package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRenderDocFileModule(t *testing.T) {
	path := writeDocTestFile(t, `
// Geometry helpers.

// Mathematical answer.
pub const i32 answer = 42;

// Adds two numbers.
pub fn add(i32 a, i32 b) -> i32 {
	return a + b;
}

// A named formatter.
pub interface Formatter {
	// Formats a label.
	fn format(string label) -> string;
}

// A point in world space.
pub class Point implements Formatter {
	// X coordinate.
	pub i32 x;

	i32 hidden;

	// Formats the point name.
	pub fn format(string label) -> string {
		return label;
	}

	fn secret() -> i32 {
		return self.hidden;
	}
}

// Private helper.
fn hidden_fn() -> i32 {
	return 0;
}
`)

	out, err := renderDocFile(path, "")
	if err != nil {
		t.Fatalf("renderDocFile() error = %v", err)
	}

	assertContains(t, out, "MODULE\n  sample")
	assertContains(t, out, "SUMMARY\n  Geometry helpers.")
	assertContains(t, out, "CONSTANTS\n  answer i32\n    Mathematical answer.")
	assertContains(t, out, "INTERFACES\n  Formatter")
	assertContains(t, out, "format(string label) -> string")
	assertContains(t, out, "CLASSES\n  Point implements Formatter")
	assertContains(t, out, "Fields\n      x i32")
	assertContains(t, out, "Methods\n      format(string label) -> string")
	assertContains(t, out, "FUNCTIONS\n  add(i32 a, i32 b) -> i32")

	if strings.Contains(out, "hidden_fn") {
		t.Fatalf("unexpected private function in output:\n%s", out)
	}
	if strings.Contains(out, "secret() -> i32") {
		t.Fatalf("unexpected private method in output:\n%s", out)
	}
	if strings.Contains(out, "hidden i32") {
		t.Fatalf("unexpected private field in output:\n%s", out)
	}
}

func TestRenderDocFileSymbolLookup(t *testing.T) {
	path := writeDocTestFile(t, `
// Geometry helpers.
pub class Point {
	// X coordinate.
	pub i32 x;

	// Measures size.
	pub fn size() -> i32 {
		return self.x;
	}
}

// Adds two numbers.
pub fn add(i32 a, i32 b) -> i32 {
	return a + b;
}
`)

	fieldOut, err := renderDocFile(path, "Point.x")
	if err != nil {
		t.Fatalf("renderDocFile(field) error = %v", err)
	}
	if got, want := fieldOut, "Point.x i32\n\nX coordinate."; got != want {
		t.Fatalf("field doc mismatch\nwant:\n%s\n\ngot:\n%s", want, got)
	}

	methodOut, err := renderDocFile(path, "Point.size")
	if err != nil {
		t.Fatalf("renderDocFile(method) error = %v", err)
	}
	if got, want := methodOut, "Point.size() -> i32\n\nMeasures size."; got != want {
		t.Fatalf("method doc mismatch\nwant:\n%s\n\ngot:\n%s", want, got)
	}

	funcOut, err := renderDocFile(path, "add")
	if err != nil {
		t.Fatalf("renderDocFile(function) error = %v", err)
	}
	if got, want := funcOut, "add(i32 a, i32 b) -> i32\n\nAdds two numbers."; got != want {
		t.Fatalf("function doc mismatch\nwant:\n%s\n\ngot:\n%s", want, got)
	}
}

func TestRenderDocFileMissingSymbol(t *testing.T) {
	path := writeDocTestFile(t, `
pub fn add(i32 a, i32 b) -> i32 {
	return a + b;
}
`)

	_, err := renderDocFile(path, "missing")
	if err == nil {
		t.Fatal("expected missing symbol error")
	}
	if !strings.Contains(err.Error(), `public symbol "missing" not found`) {
		t.Fatalf("unexpected error: %v", err)
	}
}

func writeDocTestFile(t *testing.T, contents string) string {
	t.Helper()

	dir := t.TempDir()
	path := filepath.Join(dir, "sample.basl")
	data := strings.TrimLeft(contents, "\n")
	if err := os.WriteFile(path, []byte(data), 0644); err != nil {
		t.Fatalf("os.WriteFile() error = %v", err)
	}
	return path
}

func assertContains(t *testing.T, got, want string) {
	t.Helper()
	if !strings.Contains(got, want) {
		t.Fatalf("output missing %q\nfull output:\n%s", want, got)
	}
}
