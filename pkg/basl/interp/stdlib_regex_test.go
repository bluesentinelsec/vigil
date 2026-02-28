package interp

import "testing"

func TestRegexMatch(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "regex"; fn main() -> i32 { bool m, err e = regex.match("^hello", "hello world"); fmt.print(string(m)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" {
		t.Fatalf("got %q", out[0])
	}
}

func TestRegexFind(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "regex"; fn main() -> i32 { string m, err e = regex.find("[0-9]+", "abc123def"); fmt.print(m); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "123" {
		t.Fatalf("got %q", out[0])
	}
}

func TestRegexFindAll(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "regex"; fn main() -> i32 { array<string> m, err e = regex.find_all("[0-9]+", "a1b2c3"); fmt.print(string(m.len())); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "3" {
		t.Fatalf("got %q", out[0])
	}
}

func TestRegexReplace(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "regex"; fn main() -> i32 { string r, err e = regex.replace("[0-9]", "a1b2c3", "X"); fmt.print(r); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "aXbXcX" {
		t.Fatalf("got %q", out[0])
	}
}

func TestRegexSplit(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "regex"; fn main() -> i32 { array<string> parts, err e = regex.split("[,;]", "a,b;c"); fmt.print(string(parts.len())); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "3" {
		t.Fatalf("got %q", out[0])
	}
}

func TestRegexInvalidPattern(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "regex"; fn main() -> i32 { bool m, err e = regex.match("[invalid", "test"); fmt.print(string(e)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for invalid regex pattern")
	}
}
