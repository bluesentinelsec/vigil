package main

import (
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRenderEditorListIncludesSupportedTargets(t *testing.T) {
	out := renderEditorList()

	for _, want := range []string{
		"AVAILABLE EDITORS",
		"vim",
		"nvim",
		"vscode",
	} {
		if !strings.Contains(out, want) {
			t.Fatalf("renderEditorList() missing %q:\n%s", want, out)
		}
	}
}

func TestInstallEditorsWritesManagedFiles(t *testing.T) {
	home := t.TempDir()

	if err := installEditors([]string{"vim", "vscode"}, home, false, io.Discard); err != nil {
		t.Fatalf("installEditors() error = %v", err)
	}

	for _, path := range []string{
		filepath.Join(home, ".vim", "ftdetect", "basl.vim"),
		filepath.Join(home, ".vim", "syntax", "basl.vim"),
		filepath.Join(home, ".vscode", "extensions", "basl", "package.json"),
		filepath.Join(home, ".vscode", "extensions", "basl", "syntaxes", "basl.tmLanguage.json"),
	} {
		if _, err := os.Stat(path); err != nil {
			t.Fatalf("expected installed file %s: %v", path, err)
		}
	}
}

func TestInstallEditorsRequiresForceForConflictingFiles(t *testing.T) {
	home := t.TempDir()

	if err := installEditors([]string{"vim"}, home, false, io.Discard); err != nil {
		t.Fatalf("initial installEditors() error = %v", err)
	}

	path := filepath.Join(home, ".vim", "syntax", "basl.vim")
	if err := os.WriteFile(path, []byte("custom\n"), 0644); err != nil {
		t.Fatalf("os.WriteFile() error = %v", err)
	}

	if err := installEditors([]string{"vim"}, home, false, io.Discard); err == nil {
		t.Fatal("installEditors() succeeded without --force for conflicting file")
	}

	if err := installEditors([]string{"vim"}, home, true, io.Discard); err != nil {
		t.Fatalf("forced installEditors() error = %v", err)
	}

	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("os.ReadFile() error = %v", err)
	}
	if strings.Contains(string(data), "custom") {
		t.Fatalf("force install did not overwrite file:\n%s", string(data))
	}
}

func TestUninstallEditorsRemovesInstalledFiles(t *testing.T) {
	home := t.TempDir()

	if err := installEditors([]string{"nvim", "vscode"}, home, false, io.Discard); err != nil {
		t.Fatalf("installEditors() error = %v", err)
	}
	if err := uninstallEditors([]string{"nvim", "vscode"}, home, io.Discard); err != nil {
		t.Fatalf("uninstallEditors() error = %v", err)
	}

	for _, path := range []string{
		filepath.Join(home, ".config", "nvim", "syntax", "basl.vim"),
		filepath.Join(home, ".vscode", "extensions", "basl"),
	} {
		if _, err := os.Stat(path); !os.IsNotExist(err) {
			t.Fatalf("expected %s to be removed, stat err = %v", path, err)
		}
	}
}

func TestRunEditorSemanticDiagnosticsOutputsJSON(t *testing.T) {
	dir := t.TempDir()
	path := writeDocLikeTestFile(t, dir, "main.basl", `
import "missing";

fn main() -> void {}
`)

	code, stdout, stderr := captureEditorSemantic(t, []string{"diagnostics", "--file", path})
	if code != 0 {
		t.Fatalf("runEditorSemantic() code = %d, stderr = %s", code, stderr)
	}
	if stderr != "" {
		t.Fatalf("expected empty stderr, got %q", stderr)
	}

	var out []map[string]any
	if err := json.Unmarshal([]byte(stdout), &out); err != nil {
		t.Fatalf("json.Unmarshal() error = %v\nstdout:\n%s", err, stdout)
	}
	if len(out) == 0 {
		t.Fatal("expected diagnostics output")
	}
}

func TestRunEditorSemanticDefinitionOutputsJSON(t *testing.T) {
	dir := t.TempDir()
	writeDocLikeTestFile(t, dir, "basl.toml", "name = \"demo\"\nversion = \"0.1.0\"\n")
	_ = os.MkdirAll(filepath.Join(dir, "lib"), 0755)
	writeDocLikeTestFile(t, filepath.Join(dir, "lib"), "helper.basl", `
pub fn message() -> string {
    return "hi";
}
`)
	mainPath := writeDocLikeTestFile(t, dir, "main.basl", `
import "helper";

fn main() -> void {
    string value = helper.message();
}
`)

	code, stdout, stderr := captureEditorSemantic(t, []string{
		"definition",
		"--file", mainPath,
		"--line", "4",
		"--col", "27",
	})
	if code != 0 {
		t.Fatalf("runEditorSemantic() code = %d, stderr = %s", code, stderr)
	}
	if stderr != "" {
		t.Fatalf("expected empty stderr, got %q", stderr)
	}

	var out map[string]any
	if err := json.Unmarshal([]byte(stdout), &out); err != nil {
		t.Fatalf("json.Unmarshal() error = %v\nstdout:\n%s", err, stdout)
	}
	if got := out["line"]; got != float64(1) {
		t.Fatalf("definition line = %v, want 1", got)
	}
}

func captureEditorSemantic(t *testing.T, args []string) (int, string, string) {
	t.Helper()

	origStdout := os.Stdout
	origStderr := os.Stderr
	outR, outW, err := os.Pipe()
	if err != nil {
		t.Fatalf("os.Pipe(stdout) error = %v", err)
	}
	errR, errW, err := os.Pipe()
	if err != nil {
		t.Fatalf("os.Pipe(stderr) error = %v", err)
	}
	defer outR.Close()
	defer errR.Close()

	os.Stdout = outW
	os.Stderr = errW
	code := runEditorSemantic(args)
	_ = outW.Close()
	_ = errW.Close()
	os.Stdout = origStdout
	os.Stderr = origStderr

	stdout, err := io.ReadAll(outR)
	if err != nil {
		t.Fatalf("io.ReadAll(stdout) error = %v", err)
	}
	stderr, err := io.ReadAll(errR)
	if err != nil {
		t.Fatalf("io.ReadAll(stderr) error = %v", err)
	}
	return code, strings.TrimSpace(string(stdout)), strings.TrimSpace(string(stderr))
}
