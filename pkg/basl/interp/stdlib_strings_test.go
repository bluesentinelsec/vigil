package interp

import "testing"

func TestStringsJoin(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "strings"; fn main() -> i32 { array<string> a = ["x","y","z"]; fmt.print(strings.join(a, ",")); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "x,y,z" {
		t.Fatalf("got %q", out[0])
	}
}

func TestStringsRepeat(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "strings"; fn main() -> i32 { fmt.print(strings.repeat("ab", 3)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ababab" {
		t.Fatalf("got %q", out[0])
	}
}
