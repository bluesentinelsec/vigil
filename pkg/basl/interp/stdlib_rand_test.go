package interp

import "testing"

func TestRandBytes(t *testing.T) {
	src := `import "fmt"; import "rand";
fn main() -> i32 {
	string h = rand.bytes(16);
	fmt.print(string(h.len()));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	// 16 bytes = 32 hex chars
	if out[0] != "32" {
		t.Fatalf("got %q", out[0])
	}
}

func TestRandInt(t *testing.T) {
	src := `import "fmt"; import "rand";
fn main() -> i32 {
	i32 n = rand.int(0, 100);
	fmt.print(string(n >= 0));
	fmt.print(string(n < 100));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" || out[1] != "true" {
		t.Fatalf("got %v", out)
	}
}
