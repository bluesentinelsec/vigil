package interp

import "testing"

func TestUnsafeAllocAndBuffer(t *testing.T) {
	src := `import "fmt"; import "unsafe";
fn main() -> i32 {
	unsafe.Buffer buf = unsafe.alloc(4);
	fmt.print(string(buf.len()));
	buf.set(0, 65);
	buf.set(1, 66);
	fmt.print(string(buf.get(0)));
	fmt.print(string(buf.get(1)));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "4" || out[1] != "65" || out[2] != "66" {
		t.Fatalf("got %v", out)
	}
}

func TestUnsafeAllocZero(t *testing.T) {
	_, _, err := evalBASL(`import "unsafe"; fn main() -> i32 { unsafe.Buffer buf = unsafe.alloc(0); return 0; }`)
	if err == nil {
		t.Fatal("expected error for alloc(0)")
	}
}

func TestUnsafeBufferOutOfBounds(t *testing.T) {
	_, _, err := evalBASL(`import "unsafe"; fn main() -> i32 { unsafe.Buffer buf = unsafe.alloc(2); buf.get(5); return 0; }`)
	if err == nil {
		t.Fatal("expected error for out of bounds get")
	}
}

func TestUnsafeNull(t *testing.T) {
	src := `import "fmt"; import "unsafe";
fn main() -> i32 {
	fmt.print(string(unsafe.null));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "null" {
		t.Fatalf("got %q", out[0])
	}
}

func TestUnsafeLayout(t *testing.T) {
	src := `import "fmt"; import "unsafe";
fn main() -> i32 {
	unsafe.Layout layout = unsafe.layout("i32", "i32");
	unsafe.Struct s = layout.new();
	s.set(0, 42);
	s.set(1, 99);
	fmt.print(string(s.get(0)));
	fmt.print(string(s.get(1)));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "42" || out[1] != "99" {
		t.Fatalf("got %v", out)
	}
}
