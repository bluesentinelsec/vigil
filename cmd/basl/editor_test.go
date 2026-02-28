package main

import (
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
