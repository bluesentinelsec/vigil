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

func TestDeclarationResolvesImportedModuleMember(t *testing.T) {
	_, mainPath := writeSemanticFixture(t)

	pos := mustFindPosition(t, mainPath, "message();")
	got, err := Declaration(mainPath, pos, nil)
	if err != nil {
		t.Fatalf("Declaration() error = %v", err)
	}
	if got == nil {
		t.Fatal("Declaration() returned nil location")
	}
	if filepath.Base(got.Path) != "helper.basl" {
		t.Fatalf("declaration path = %s, want helper.basl", got.Path)
	}
	if got.Line != 13 {
		t.Fatalf("declaration line = %d, want 13", got.Line)
	}
}

func TestImplementationsResolveInterfaceTargets(t *testing.T) {
	root := t.TempDir()
	mainPath := filepath.Join(root, "main.basl")
	src := `interface Greeter {
    fn greet(string name) -> string;
}

class Person implements Greeter {
    pub fn greet(string name) -> string {
        return "hello " + name;
    }
}

class Robot implements Greeter {
    pub fn greet(string name) -> string {
        return "beep " + name;
    }
}
`
	if err := os.WriteFile(mainPath, []byte(src), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}

	items, err := Implementations(mainPath, mustFindPosition(t, mainPath, "Greeter {"), nil)
	if err != nil {
		t.Fatalf("Implementations(interface) error = %v", err)
	}
	if len(items) != 2 {
		t.Fatalf("Implementations(interface) len = %d, want 2", len(items))
	}

	methodItems, err := Implementations(mainPath, mustFindPosition(t, mainPath, "greet(string name) -> string;"), nil)
	if err != nil {
		t.Fatalf("Implementations(method) error = %v", err)
	}
	if len(methodItems) != 2 {
		t.Fatalf("Implementations(method) len = %d, want 2", len(methodItems))
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

func TestDocumentHighlightsIncludeDeclarationAndUse(t *testing.T) {
	_, mainPath := writeSemanticFixture(t)

	items, err := DocumentHighlights(mainPath, mustFindPosition(t, mainPath, "value = helper"), nil)
	if err != nil {
		t.Fatalf("DocumentHighlights() error = %v", err)
	}
	if len(items) != 2 {
		t.Fatalf("DocumentHighlights() len = %d, want 2", len(items))
	}
	var sawWrite bool
	var textCount int
	for _, item := range items {
		switch item.Kind {
		case "write":
			sawWrite = true
		case "text":
			textCount++
		}
	}
	if !sawWrite || textCount != 1 {
		t.Fatalf("document highlights = %#v, want declaration write + 1 usage text highlight", items)
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

func TestWorkspaceSymbolsSpanFilesAndMembers(t *testing.T) {
	_, mainPath := writeSemanticFixture(t)

	items, err := WorkspaceSymbols(mainPath, "greet", nil)
	if err != nil {
		t.Fatalf("WorkspaceSymbols() error = %v", err)
	}
	if len(items) == 0 {
		t.Fatal("WorkspaceSymbols() returned no symbols")
	}

	var sawGreeterMethod bool
	for _, item := range items {
		if item.Name == "greet" && item.ContainerName == "Greeter" && filepath.Base(item.Location.Path) == "helper.basl" {
			sawGreeterMethod = true
			break
		}
	}
	if !sawGreeterMethod {
		t.Fatalf("workspace symbols = %#v, want Greeter.greet from helper.basl", items)
	}
}

func TestBuiltinMetadataFeedsHoverCompletionsAndSignatureHelp(t *testing.T) {
	_, mainPath := writeSemanticFixture(t)

	hoverPos := mustFindPosition(t, mainPath, "println(value")
	hover, err := HoverAt(mainPath, hoverPos, nil)
	if err != nil {
		t.Fatalf("HoverAt() error = %v", err)
	}
	if hover == nil || !strings.Contains(hover.Contents, "fmt.println(val) -> err") {
		t.Fatalf("hover = %#v, want stdlib signature", hover)
	}

	items, err := Completions(mainPath, mustFindPositionAfter(t, mainPath, "fmt."), nil)
	if err != nil {
		t.Fatalf("Completions() error = %v", err)
	}
	var printlnItem *CompletionItem
	for i := range items {
		if items[i].Label == "println" {
			printlnItem = &items[i]
			break
		}
	}
	if printlnItem == nil || printlnItem.Documentation == "" {
		t.Fatalf("builtin completion = %#v, want documentation", printlnItem)
	}

	help, err := SignatureHelpAt(mainPath, mustFindPositionAfter(t, mainPath, "fmt.println("), nil)
	if err != nil {
		t.Fatalf("SignatureHelpAt() error = %v", err)
	}
	if help == nil || len(help.Signatures) == 0 || help.Signatures[0].Label != "fmt.println(val) -> err" {
		t.Fatalf("signature help = %#v, want fmt.println signature", help)
	}
}

func TestSemanticTokensCoverKeywordsSymbolsAndBuiltins(t *testing.T) {
	root := t.TempDir()
	mainPath := filepath.Join(root, "main.basl")
	src := `import "fmt";
// announce birthday flow
const i32 answer = 42;

class Person {
    pub string name;

    pub fn greet(string msg) -> void {
        fmt.println(msg);
    }
}

fn main() -> void {
    Person alice = Person();
    alice.greet("hi");
    fmt.println(answer);
}
`
	if err := os.WriteFile(mainPath, []byte(src), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}

	items, err := SemanticTokens(mainPath, nil)
	if err != nil {
		t.Fatalf("SemanticTokens() error = %v", err)
	}
	if len(items) == 0 {
		t.Fatal("SemanticTokens() returned no tokens")
	}

	assertSemanticToken(t, src, items, "import", "keyword")
	assertSemanticToken(t, src, items, "// announce birthday flow", "comment")
	assertSemanticToken(t, src, items, "i32", "type")
	assertSemanticToken(t, src, items, "42", "number")
	assertSemanticToken(t, src, items, "Person", "class", "declaration")
	assertSemanticToken(t, src, items, "name", "property", "declaration")
	assertSemanticToken(t, src, items, "greet", "method", "declaration")
	assertSemanticToken(t, src, items, "msg", "parameter", "declaration")
	assertSemanticToken(t, src, items, "alice", "variable", "declaration")
	assertSemanticToken(t, src, items, "fmt", "namespace", "defaultLibrary")
	assertSemanticToken(t, src, items, "println", "function", "defaultLibrary")
	assertSemanticToken(t, src, items, "answer", "variable", "readonly")
}

func TestCodeActionsProvideMissingImportQuickFixAndOrganizeImports(t *testing.T) {
	root := t.TempDir()
	if err := os.WriteFile(filepath.Join(root, "basl.toml"), []byte("name = \"demo\"\nversion = \"0.1.0\"\n"), 0644); err != nil {
		t.Fatalf("os.WriteFile(basl.toml) error = %v", err)
	}
	mainPath := filepath.Join(root, "main.basl")
	src := `import "helper";
import "fmt";

fn main() -> void {
    fmt.println("hi");
}
`
	if err := os.WriteFile(mainPath, []byte(src), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}

	actions, err := CodeActions(mainPath, nil, []string{"source.organizeImports"}, nil)
	if err != nil {
		t.Fatalf("CodeActions(organize) error = %v", err)
	}
	if len(actions) != 1 || actions[0].Kind != "source.organizeImports" {
		t.Fatalf("organize imports actions = %#v, want organizeImports action", actions)
	}
	if len(actions[0].Edits) != 1 || !strings.Contains(actions[0].Edits[0].NewText, "import \"fmt\";\nimport \"helper\";\n") {
		t.Fatalf("organize imports edit = %#v, want sorted imports", actions[0].Edits)
	}

	missingPath := filepath.Join(root, "missing.basl")
	missingSrc := `fn main() -> void {
    fmt.println("hi");
}
`
	if err := os.WriteFile(missingPath, []byte(missingSrc), 0644); err != nil {
		t.Fatalf("os.WriteFile(missing) error = %v", err)
	}
	diags, err := Diagnostics(missingPath, nil)
	if err != nil {
		t.Fatalf("Diagnostics(missing) error = %v", err)
	}
	var messages []string
	for _, diag := range diags {
		messages = append(messages, diag.Message)
	}
	actions, err = CodeActions(missingPath, messages, []string{"quickfix"}, nil)
	if err != nil {
		t.Fatalf("CodeActions(quickfix) error = %v", err)
	}
	if len(actions) == 0 {
		t.Fatal("CodeActions(quickfix) returned no actions")
	}
	found := false
	for _, action := range actions {
		if action.Title == `Add import "fmt"` && len(action.Edits) == 1 && strings.Contains(action.Edits[0].NewText, "import \"fmt\";") {
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("quickfix actions = %#v, want Add import \"fmt\"", actions)
	}
}

func TestPrepareRenameAndFormatDocument(t *testing.T) {
	_, mainPath := writeSemanticFixture(t)

	item, err := PrepareRenameAt(mainPath, mustFindPosition(t, mainPath, "value = helper"), nil)
	if err != nil {
		t.Fatalf("PrepareRenameAt() error = %v", err)
	}
	if item == nil || item.Placeholder != "value" {
		t.Fatalf("prepare rename = %#v, want placeholder value", item)
	}
	endPos := mustFindPositionAfter(t, mainPath, "value")
	item, err = PrepareRenameAt(mainPath, endPos, nil)
	if err != nil {
		t.Fatalf("PrepareRenameAt(end) error = %v", err)
	}
	if item == nil || item.Placeholder != "value" {
		t.Fatalf("prepare rename at end = %#v, want placeholder value", item)
	}
	if _, err := Rename(mainPath, mustFindPosition(t, mainPath, "value = helper"), "not valid", nil); err == nil {
		t.Fatal("Rename() with invalid identifier succeeded, want error")
	}

	root := t.TempDir()
	path := filepath.Join(root, "main.basl")
	if err := os.WriteFile(path, []byte("fn main() -> void {fmt.println(\"hi\");}\n"), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}
	formatted, err := FormatDocument(path, nil)
	if err != nil {
		t.Fatalf("FormatDocument() error = %v", err)
	}
	if !strings.Contains(formatted, "fn main() -> void {\n") || !strings.Contains(formatted, "    fmt.println(\"hi\");\n") {
		t.Fatalf("formatted output = %q, want normalized layout", formatted)
	}
}

func TestRenameIncludesFStringInterpolations(t *testing.T) {
	root := t.TempDir()
	mainPath := filepath.Join(root, "main.basl")
	src := `import "fmt";

fn main() -> i32 {
    i32 foo = 1;
    fmt.println("Hello, World!");
    fmt.println(f"Test: {foo}");
    foo = 2;
    return 0;
}
`
	if err := os.WriteFile(mainPath, []byte(src), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}

	pos := mustFindPosition(t, mainPath, "foo = 1")
	refs, err := References(mainPath, pos, nil)
	if err != nil {
		t.Fatalf("References() error = %v", err)
	}
	if len(refs) != 3 {
		t.Fatalf("References() len = %d, want 3 including f-string use", len(refs))
	}

	edits, err := Rename(mainPath, pos, "bar", nil)
	if err != nil {
		t.Fatalf("Rename() error = %v", err)
	}
	if len(edits) != 3 {
		t.Fatalf("Rename() len = %d, want 3 including f-string use", len(edits))
	}
}

func TestRenameIncludesMethodDeclarations(t *testing.T) {
	root := t.TempDir()
	mainPath := filepath.Join(root, "main.basl")
	src := `import "fmt";

class Person {
    pub string name;

    fn init(string name) -> void {
        self.name = name;
    }

    pub fn birthday() -> void {
        fmt.println(f"Happy birthday, {self.name}");
    }
}

fn main() -> i32 {
    Person alice = Person("Alice");
    alice.birthday();
    return 0;
}
`
	if err := os.WriteFile(mainPath, []byte(src), 0644); err != nil {
		t.Fatalf("os.WriteFile(main) error = %v", err)
	}

	callPos := mustFindPosition(t, mainPath, "birthday();")
	edits, err := Rename(mainPath, callPos, "get_birthday", nil)
	if err != nil {
		t.Fatalf("Rename(call) error = %v", err)
	}
	if len(edits) != 2 {
		t.Fatalf("Rename(call) len = %d, want 2 including declaration", len(edits))
	}

	var sawDecl bool
	var sawCall bool
	for _, edit := range edits {
		if edit.NewText != "get_birthday" {
			t.Fatalf("rename new text = %q, want get_birthday", edit.NewText)
		}
		lineText := lineAt(t, mainPath, edit.Line)
		if strings.Contains(lineText, "pub fn birthday()") {
			sawDecl = true
		}
		if strings.Contains(lineText, "alice.birthday();") {
			sawCall = true
		}
	}
	if !sawDecl || !sawCall {
		t.Fatalf("rename edits missing declaration or call site: %#v", edits)
	}

	declPos := mustFindPosition(t, mainPath, "birthday() -> void")
	item, err := PrepareRenameAt(mainPath, declPos, nil)
	if err != nil {
		t.Fatalf("PrepareRenameAt(decl) error = %v", err)
	}
	if item == nil || item.Placeholder != "birthday" {
		t.Fatalf("prepare rename from declaration = %#v, want placeholder birthday", item)
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

func lineAt(t *testing.T, path string, line int) string {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("os.ReadFile(%s) error = %v", path, err)
	}
	lines := strings.Split(string(data), "\n")
	if line <= 0 || line > len(lines) {
		t.Fatalf("lineAt(%s, %d) out of range", path, line)
	}
	return lines[line-1]
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

func assertSemanticToken(t *testing.T, src string, items []SemanticToken, text string, wantType string, modifiers ...string) {
	t.Helper()
	for _, item := range items {
		if semanticTokenText(t, src, item.Location) != text {
			continue
		}
		if item.Type != wantType {
			continue
		}
		if !semanticModifiersInclude(item.Modifiers, modifiers...) {
			continue
		}
		return
	}
	t.Fatalf("semantic token %q of type %q with modifiers %v not found in %#v", text, wantType, modifiers, items)
}

func semanticTokenText(t *testing.T, src string, loc Location) string {
	t.Helper()
	lines := strings.Split(src, "\n")
	if loc.Line <= 0 || loc.Line > len(lines) {
		t.Fatalf("semantic token line %d out of range", loc.Line)
	}
	line := lines[loc.Line-1]
	start := loc.Col - 1
	end := loc.EndCol
	if start < 0 || end < start || end > len(line) {
		t.Fatalf("semantic token columns %d:%d out of range for line %q", loc.Col, loc.EndCol, line)
	}
	return line[start:end]
}

func semanticModifiersInclude(got []string, want ...string) bool {
	if len(want) == 0 {
		return true
	}
	set := make(map[string]bool, len(got))
	for _, item := range got {
		set[item] = true
	}
	for _, item := range want {
		if !set[item] {
			return false
		}
	}
	return true
}
