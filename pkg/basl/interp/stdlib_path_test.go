package interp

import "testing"

func TestPathJoin(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "path"; fn main() -> i32 { fmt.print(path.join("a", "b", "c")); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "a/b/c" {
		t.Fatalf("got %q", out[0])
	}
}

func TestPathDirBaseExt(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "path"; fn main() -> i32 { fmt.print(path.dir("/a/b/c.txt")); fmt.print(path.base("/a/b/c.txt")); fmt.print(path.ext("/a/b/c.txt")); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "/a/b" || out[1] != "c.txt" || out[2] != ".txt" {
		t.Fatalf("got %v", out)
	}
}
