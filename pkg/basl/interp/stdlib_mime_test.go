package interp

import "testing"

func TestMimeTypeByExt(t *testing.T) {
	src := `import "fmt"; import "mime";
fn main() -> i32 {
	fmt.print(mime.type_by_ext(".json"));
	fmt.print(mime.type_by_ext("html"));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "application/json" {
		t.Fatalf("got %q for .json", out[0])
	}
	if out[1] != "text/html; charset=utf-8" && out[1] != "text/html" {
		t.Fatalf("got %q for html", out[1])
	}
}
