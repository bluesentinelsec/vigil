package interp

import "testing"

func TestBase64EncodeDecode(t *testing.T) {
	src := `import "fmt"; import "base64";
fn main() -> i32 {
	string encoded = base64.encode("hello world");
	fmt.print(encoded);
	string decoded, err e = base64.decode(encoded);
	fmt.print(decoded);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "aGVsbG8gd29ybGQ=" || out[1] != "hello world" {
		t.Fatalf("got %v", out)
	}
}

func TestBase64DecodeInvalid(t *testing.T) {
	src := `import "fmt"; import "base64";
fn main() -> i32 {
	string decoded, err e = base64.decode("!!!invalid!!!");
	fmt.print(string(e));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for invalid base64")
	}
}
