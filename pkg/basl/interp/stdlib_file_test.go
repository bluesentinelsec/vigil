package interp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func escapePathForBASL(path string) string {
	return strings.ReplaceAll(path, `\`, `\\`)
}

func TestFileWriteReadAll(t *testing.T) {
	tmp := escapePathForBASL(filepath.Join(t.TempDir(), "test.txt"))
	src := `import "fmt"; import "file"; fn main() -> i32 { file.write_all("` + tmp + `", "hello"); string data, err e = file.read_all("` + tmp + `"); fmt.print(data); return 0; }`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "hello" {
		t.Fatalf("got %q", out[0])
	}
}

func TestFileReadAllNotFound(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "file"; fn main() -> i32 { string data, err e = file.read_all("/nonexistent/path"); fmt.print(string(e)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for nonexistent file")
	}
}

func TestFileExists(t *testing.T) {
	tmp := escapePathForBASL(filepath.Join(t.TempDir(), "exists.txt"))
	os.WriteFile(strings.ReplaceAll(tmp, `\\`, `\`), []byte("x"), 0644)
	_, out, err := evalBASL(`import "fmt"; import "file"; fn main() -> i32 { fmt.print(string(file.exists("` + tmp + `"))); fmt.print(string(file.exists("/no/such/file"))); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" || out[1] != "false" {
		t.Fatalf("got %v", out)
	}
}

func TestFileMkdirListDir(t *testing.T) {
	tmp := escapePathForBASL(filepath.Join(t.TempDir(), "sub"))
	tmpFile := escapePathForBASL(filepath.Join(strings.ReplaceAll(tmp, `\\`, `\`), "a.txt"))
	src := `import "fmt"; import "file"; fn main() -> i32 { file.mkdir("` + tmp + `"); file.write_all("` + tmpFile + `", "a"); array<string> entries, err e = file.list_dir("` + tmp + `"); fmt.print(string(entries.len())); return 0; }`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "1" {
		t.Fatalf("got %q", out[0])
	}
}

func TestFileWalkFailFast(t *testing.T) {
	root := t.TempDir()
	sub := filepath.Join(root, "sub")
	if err := os.MkdirAll(sub, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "a.txt"), []byte("a"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(sub, "b.txt"), []byte("b"), 0644); err != nil {
		t.Fatal(err)
	}

	rootEsc := escapePathForBASL(root)
	src := `import "fmt"; import "file";
fn main() -> i32 {
    array<file.Entry> entries, err e = file.walk("` + rootEsc + `");
    i32 dirs = 0;
    for (i32 i = 0; i < entries.len(); i++) {
        if (entries[i].is_dir) {
            dirs++;
        }
    }
    fmt.print(string(e == ok) + ":" + string(entries.len()) + ":" + string(dirs));
    return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true:4:2" {
		t.Fatalf("got %q", out[0])
	}
}

func TestFileWalkBestEffortCollectsIssues(t *testing.T) {
	missing := escapePathForBASL(filepath.Join(t.TempDir(), "missing"))
	src := `import "fmt"; import "file";
fn main() -> i32 {
    array<file.Entry> entries, array<file.WalkIssue> issues = file.walk_best_effort("` + missing + `");
    fmt.print(string(entries.len()) + ":" + string(issues.len()) + ":" + issues[0].err.kind());
    return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "0:1:not_found" {
		t.Fatalf("got %q", out[0])
	}
}

func TestFileWalkFollowLinksCycleSafe(t *testing.T) {
	root := t.TempDir()
	sub := filepath.Join(root, "sub")
	if err := os.MkdirAll(sub, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "a.txt"), []byte("a"), 0644); err != nil {
		t.Fatal(err)
	}
	loop := filepath.Join(sub, "loop")
	if err := os.Symlink("..", loop); err != nil {
		t.Skipf("symlink unavailable: %v", err)
	}

	rootEsc := escapePathForBASL(root)
	src := `import "fmt"; import "file";
fn main() -> i32 {
    array<file.Entry> strictEntries, err strictErr = file.walk_follow_links("` + rootEsc + `");
    array<file.Entry> bestEntries, array<file.WalkIssue> bestIssues = file.walk_follow_links_best_effort("` + rootEsc + `");
    fmt.print(string(strictEntries.len()) + ":" + strictErr.kind() + ":" + string(bestEntries.len()) + ":" + string(bestIssues.len()) + ":" + bestIssues[0].err.kind());
    return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "0:state:4:1:state" {
		t.Fatalf("got %q", out[0])
	}
}
