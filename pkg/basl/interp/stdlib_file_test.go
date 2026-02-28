package interp

import (
	"os"
	"path/filepath"
	"testing"
)

func TestFileWriteReadAll(t *testing.T) {
	tmp := filepath.Join(t.TempDir(), "test.txt")
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
	tmp := filepath.Join(t.TempDir(), "exists.txt")
	os.WriteFile(tmp, []byte("x"), 0644)
	_, out, err := evalBASL(`import "fmt"; import "file"; fn main() -> i32 { fmt.print(string(file.exists("` + tmp + `"))); fmt.print(string(file.exists("/no/such/file"))); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" || out[1] != "false" {
		t.Fatalf("got %v", out)
	}
}

func TestFileMkdirListDir(t *testing.T) {
	tmp := filepath.Join(t.TempDir(), "sub")
	tmpFile := filepath.Join(tmp, "a.txt")
	src := `import "fmt"; import "file"; fn main() -> i32 { file.mkdir("` + tmp + `"); file.write_all("` + tmpFile + `", "a"); array<string> entries, err e = file.list_dir("` + tmp + `"); fmt.print(string(entries.len())); return 0; }`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "1" {
		t.Fatalf("got %q", out[0])
	}
}
