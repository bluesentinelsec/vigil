package interp

import (
	"path/filepath"
	"strings"
	"testing"
)

func TestPathJoin(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "path"; fn main() -> i32 { fmt.print(path.join("a", "b", "c")); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	expected := filepath.Join("a", "b", "c")
	if out[0] != expected {
		t.Fatalf("got %q, want %q", out[0], expected)
	}
}

func TestPathDirBaseExt(t *testing.T) {
	testPath := strings.ReplaceAll(filepath.Join("/a", "b", "c.txt"), `\`, `\\`)
	code := `import "fmt"; import "path"; fn main() -> i32 { fmt.print(path.dir("` + testPath + `")); fmt.print(path.base("` + testPath + `")); fmt.print(path.ext("` + testPath + `")); return 0; }`
	_, out, err := evalBASL(code)
	if err != nil {
		t.Fatal(err)
	}
	expectedDir := filepath.Join("/a", "b")
	if out[0] != expectedDir || out[1] != "c.txt" || out[2] != ".txt" {
		t.Fatalf("got %v, want [%q, \"c.txt\", \".txt\"]", out, expectedDir)
	}
}
