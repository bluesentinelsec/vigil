package interp

import "testing"

func TestJsonParseAndGet(t *testing.T) {
	src := `import "fmt"; import "json";
fn main() -> i32 {
	json.Value v, err e = json.parse("{\"name\":\"alice\",\"age\":30}");
	fmt.print(v.get_string("name"));
	fmt.print(string(v.get_i32("age")));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "alice" || out[1] != "30" {
		t.Fatalf("got %v", out)
	}
}

func TestJsonStringify(t *testing.T) {
	src := `import "fmt"; import "json";
fn main() -> i32 {
	json.Value v, err e = json.parse("{\"x\":1}");
	string s = json.stringify(v);
	fmt.print(s);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != `{"x":1}` {
		t.Fatalf("got %q", out[0])
	}
}

func TestJsonParseInvalid(t *testing.T) {
	src := `import "fmt"; import "json";
fn main() -> i32 {
	json.Value v, err e = json.parse("not json");
	fmt.print(string(e));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for invalid JSON")
	}
}
