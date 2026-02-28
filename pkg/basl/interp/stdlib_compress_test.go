package interp

import "testing"

func TestCompressGzipRoundtrip(t *testing.T) {
	src := `import "fmt"; import "compress";
fn main() -> i32 {
	string compressed, err e1 = compress.gzip("hello world");
	string decompressed, err e2 = compress.gunzip(compressed);
	fmt.print(decompressed);
	fmt.print(string(e1));
	fmt.print(string(e2));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "hello world" || out[1] != "ok" || out[2] != "ok" {
		t.Fatalf("got %v", out)
	}
}

func TestCompressZlibRoundtrip(t *testing.T) {
	src := `import "fmt"; import "compress";
fn main() -> i32 {
	string compressed, err e1 = compress.zlib("test data");
	string decompressed, err e2 = compress.unzlib(compressed);
	fmt.print(decompressed);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "test data" {
		t.Fatalf("got %q", out[0])
	}
}

func TestCompressGunzipInvalid(t *testing.T) {
	src := `import "fmt"; import "compress";
fn main() -> i32 {
	string data, err e = compress.gunzip("not gzip data");
	fmt.print(string(e));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for invalid gzip data")
	}
}
