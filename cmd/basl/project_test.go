package main

import (
	"path/filepath"
	"testing"
)

func TestResolveScriptAndTestSearchPathsUseProjectLayout(t *testing.T) {
	root := t.TempDir()

	writeDocLikeTestFile(t, root, "basl.toml", "name = \"demo\"\nversion = \"0.1.0\"\n")
	scriptPath := writeDocLikeTestFile(t, root, "main.basl", "fn main() -> i32 { return 0; }\n")
	testPath := writeDocLikeTestFile(t, root, "test/demo_test.basl", "fn test_ok() -> void {}\n")

	scriptPaths, err := resolveScriptSearchPaths(scriptPath, nil)
	if err != nil {
		t.Fatalf("resolveScriptSearchPaths() error = %v", err)
	}
	for _, want := range []string{
		root,
		filepath.Join(root, "lib"),
		filepath.Join(root, "deps"),
	} {
		if !containsPath(scriptPaths, want) {
			t.Fatalf("script search paths missing %q: %#v", want, scriptPaths)
		}
	}

	testPaths, err := resolveTestSearchPaths(testPath)
	if err != nil {
		t.Fatalf("resolveTestSearchPaths() error = %v", err)
	}
	for _, want := range []string{
		filepath.Join(root, "lib"),
		filepath.Join(root, "deps"),
	} {
		if !containsPath(testPaths, want) {
			t.Fatalf("test search paths missing %q: %#v", want, testPaths)
		}
	}
}

func TestDefaultTestTargetsUsesProjectTestDir(t *testing.T) {
	root := t.TempDir()
	writeDocLikeTestFile(t, root, "basl.toml", "name = \"demo\"\nversion = \"0.1.0\"\n")
	writeDocLikeTestFile(t, root, "test/demo_test.basl", "fn test_ok() -> void {}\n")

	t.Chdir(root)

	targets, err := defaultTestTargets()
	if err != nil {
		t.Fatalf("defaultTestTargets() error = %v", err)
	}
	if len(targets) != 1 || targets[0] != filepath.Join(root, "test") {
		t.Fatalf("unexpected default test targets: %#v", targets)
	}
}

func containsPath(paths []string, want string) bool {
	for _, path := range paths {
		if path == want {
			return true
		}
	}
	return false
}
