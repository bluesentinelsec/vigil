package interp

import "testing"

func TestHashSha256(t *testing.T) {
	src := `import "fmt"; import "hash";
fn main() -> i32 {
	string h = hash.sha256("hello");
	fmt.print(h);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	// SHA-256 of "hello" is well-known
	if out[0] != "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824" {
		t.Fatalf("got %q", out[0])
	}
}

func TestHashMd5(t *testing.T) {
	src := `import "fmt"; import "hash";
fn main() -> i32 {
	string h = hash.md5("hello");
	fmt.print(h);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "5d41402abc4b2a76b9719d911017c592" {
		t.Fatalf("got %q", out[0])
	}
}
