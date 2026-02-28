package interp

import (
	"testing"
)

func TestFmtPrint(t *testing.T) {
	code, out, err := evalBASL(`import "fmt"; fn main() -> i32 { fmt.print("hello"); return 0; }`)
	if err != nil || code != 0 {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(out) != 1 || out[0] != "hello" {
		t.Fatalf("got %v", out)
	}
}

func TestFmtPrintln(t *testing.T) {
	code, out, err := evalBASL(`import "fmt"; fn main() -> i32 { fmt.println("hi"); return 0; }`)
	if err != nil || code != 0 {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(out) != 1 || out[0] != "hi" {
		t.Fatalf("got %v", out)
	}
}

func TestFmtSprintf(t *testing.T) {
	code, out, err := evalBASL(`import "fmt"; fn main() -> i32 { fmt.print(fmt.sprintf("x=%d", 42)); return 0; }`)
	if err != nil || code != 0 {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(out) != 1 || out[0] != "x=42" {
		t.Fatalf("got %v", out)
	}
}

func TestFmtDollar(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want string
	}{
		{"f64", `import "fmt"; fn main() -> i32 { fmt.print(fmt.dollar(9.99)); return 0; }`, "$9.99"},
		{"i32", `import "fmt"; fn main() -> i32 { fmt.print(fmt.dollar(5)); return 0; }`, "$5.00"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if len(out) != 1 || out[0] != tt.want {
				t.Fatalf("got %v, want %q", out, tt.want)
			}
		})
	}
}

func TestFmtPrintArgCount(t *testing.T) {
	_, _, err := evalBASL(`import "fmt"; fn main() -> i32 { fmt.print("a", "b"); return 0; }`)
	if err == nil {
		t.Fatal("expected error for wrong arg count")
	}
}
