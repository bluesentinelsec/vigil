package editor

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestDefinitionResolvesImportedModuleMember(t *testing.T) {
	root, mainPath := writeSemanticFixture(t)
	_ = root

	pos := mustFindPosition(t, mainPath, "message();")
	got, err := Definition(mainPath, pos, nil)
	if err != nil {
		t.Fatalf("Definition() error = %v", err)
	}
	if got == nil {
		t.Fatal("Definition() returned nil location")
	}
	if filepath.Base(got.Path) != "helper.basl" {
		t.Fatalf("definition path = %s, want helper.basl", got.Path)
	}
	if got.Line != 13 {
		t.Fatalf("definition line = %d, want 13", got.Line)
	}
}

func TestHoverReferencesAndRenameForLocalVariable(t *testing.T) {
	_, mainPath := writeSemanticFixture(t)

	pos := mustFindPosition(t, mainPath, "value = helper")
	a, file, err := newAnalyzer(mainPath, nil)
	if err != nil {
		t.Fatalf("newAnalyzer() error = %v", err)
	}
	_ = a
	var lineMatches []occurrence
	for _, occ := range file.occurrences {
		if occ.location.Line == pos.Line {
			lineMatches = append(lineMatches, occ)
		}
	}
	if len(lineMatches) == 0 {
		t.Fatalf("no occurrences recorded on line %d", pos.Line)
	}

	hover, err := HoverAt(mainPath, pos, nil)
	if err != nil {
		t.Fatalf("HoverAt() error = %v", err)
	}
	if hover == nil || !strings.Contains(hover.Contents, "string value") {
		t.Fatalf("hover = %#v, want string value detail", hover)
	}

	refs, err := References(mainPath, pos, nil)
	if err != nil {
		t.Fatalf("References() error = %v", err)
	}
	if len(refs) != 2 {
		t.Fatalf("References() len = %d, want 2", len(refs))
	}

	edits, err := Rename(mainPath, pos, "messageValue", nil)
	if err != nil {
		t.Fatalf("Rename() error = %v", err)
	}
	if len(edits) != 2 {
		t.Fatalf("Rename() len = %d, want 2", len(edits))
	}
	for _, edit := range edits {
		if edit.NewText != "messageValue" {
			t.Fatalf("rename new text = %q, want messageValue", edit.NewText)
		}
	}
}

func TestReferencesAndRenameSpanWorkspaceForExportedSymbol(t *testing.T) {
	_, mainPath := writeSemanticFixture(t)

	pos := mustFindPosition(t, mainPath, "message();")
	refs, err := References(mainPath, pos, nil)
	if err != nil {
		t.Fatalf("References() error = %v", err)
	}
	if len(refs) < 2 {
		t.Fatalf("References() len = %d, want at least 2", len(refs))
	}

	edits, err := Rename(mainPath, pos, "welcomeMessage", nil)
	if err != nil {
		t.Fatalf("Rename() error = %v", err)
	}
	if len(edits) < 2 {
		t.Fatalf("Rename() len = %d, want at least 2", len(edits))
	}
	seenHelper := false
	seenMain := false
	for _, edit := range edits {
		if filepath.Base(edit.Path) == "helper.basl" {
			seenHelper = true
		}
		if filepath.Base(edit.Path) == "main.basl" {
			seenMain = true
		}
	}
	if !seenHelper || !seenMain {
		t.Fatalf("rename edits did not cover both definition and usage: %#v", edits)
	}
}

func TestCompletionsIncludeImportedModuleAndTypedInstanceMembers(t *testing.T) {
	_, mainPath := writeSemanticFixture(t)
	a, file, err := newAnalyzer(mainPath, nil)
	if err != nil {
		t.Fatalf("newAnalyzer() error = %v", err)
	}
	if file.imports["helper"] == nil || file.imports["helper"].typ.module == nil {
		t.Fatalf("helper import not resolved: %#v", file.imports["helper"])
	}
	if _, ok := file.imports["helper"].typ.module.topLevel["message"]; !ok {
		t.Fatalf("helper module exports = %#v", file.imports["helper"].typ.module.topLevel)
	}
	_ = a

	modulePos := mustFindPositionAfter(t, mainPath, "helper.")
	items, err := Completions(mainPath, modulePos, nil)
	if err != nil {
		t.Fatalf("Completions(module) error = %v", err)
	}
	if !hasCompletion(items, "Greeter") || !hasCompletion(items, "message") {
		t.Fatalf("module completions missing imported exports: %#v", items)
	}

	instancePos := mustFindPositionAfter(t, mainPath, "g.")
	items, err = Completions(mainPath, instancePos, nil)
	if err != nil {
		t.Fatalf("Completions(instance) error = %v", err)
	}
	if !hasCompletion(items, "greet") || !hasCompletion(items, "prefix") {
		t.Fatalf("instance completions missing class members: %#v", items)
	}
}

func TestDiagnosticsUseChecker(t *testing.T) {
	root := t.TempDir()
	mainPath := filepath.Join(root, "main.basl")
	if err := os.WriteFile(mainPath, []byte("import \"missing\";\nfn main() -> void {}\n"), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}

	diags, err := Diagnostics(mainPath, nil)
	if err != nil {
		t.Fatalf("Diagnostics() error = %v", err)
	}
	if len(diags) == 0 {
		t.Fatal("Diagnostics() returned no diagnostics")
	}
}

func writeSemanticFixture(t *testing.T) (string, string) {
	t.Helper()

	root := t.TempDir()
	if err := os.WriteFile(filepath.Join(root, "basl.toml"), []byte("name = \"demo\"\nversion = \"0.1.0\"\n"), 0644); err != nil {
		t.Fatalf("os.WriteFile(basl.toml) error = %v", err)
	}
	if err := os.MkdirAll(filepath.Join(root, "lib"), 0755); err != nil {
		t.Fatalf("os.MkdirAll(lib) error = %v", err)
	}
	helper := `pub class Greeter {
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
`
	main := `import "fmt";
import "helper";

fn main() -> void {
    helper.Greeter g = helper.Greeter();
    string value = helper.message();
    fmt.println(value);
    g.greet("again");
}
`
	if err := os.WriteFile(filepath.Join(root, "lib", "helper.basl"), []byte(helper), 0644); err != nil {
		t.Fatalf("os.WriteFile(helper) error = %v", err)
	}
	mainPath := filepath.Join(root, "main.basl")
	if err := os.WriteFile(mainPath, []byte(main), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}
	return root, mainPath
}

func mustFindPosition(t *testing.T, path string, needle string) Position {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("os.ReadFile(%s) error = %v", path, err)
	}
	src := string(data)
	idx := strings.Index(src, needle)
	if idx < 0 {
		t.Fatalf("needle %q not found in %s", needle, path)
	}
	return indexToPosition(src, idx)
}

func mustFindPositionAfter(t *testing.T, path string, needle string) Position {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("os.ReadFile(%s) error = %v", path, err)
	}
	src := string(data)
	idx := strings.Index(src, needle)
	if idx < 0 {
		t.Fatalf("needle %q not found in %s", needle, path)
	}
	return indexToPosition(src, idx+len(needle))
}

func indexToPosition(src string, idx int) Position {
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

func hasCompletion(items []CompletionItem, want string) bool {
	for _, item := range items {
		if item.Label == want {
			return true
		}
	}
	return false
}
