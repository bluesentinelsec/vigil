package parser

import (
	"strings"
	"testing"

	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
)

func parseSource(src string) (int, error) {
	tokens, err := lexer.New(src).Tokenize()
	if err != nil {
		return 0, err
	}
	prog, err := New(tokens).Parse()
	if err != nil {
		return 0, err
	}
	return len(prog.Decls), nil
}

func TestParse_VarDecls(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"i32", "i32 x = 5;"},
		{"string", `string s = "hello";`},
		{"bool", "bool b = true;"},
		{"f64", "f64 f = 3.14;"},
		{"i64", "i64 n = 100;"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			n, err := parseSource(tt.src)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}
			if n != 1 {
				t.Errorf("got %d decls, want 1", n)
			}
		})
	}
}

func TestParse_Functions(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"no_params", "fn foo() {}"},
		{"with_return", "fn add(i32 a, i32 b) -> i32 { return a + b; }"},
		{"multi_return", "fn bar() -> (i32, err) { return 1; }"},
		{"void", "fn noop() { return; }"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			n, err := parseSource(tt.src)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}
			if n != 1 {
				t.Errorf("got %d decls, want 1", n)
			}
		})
	}
}

func TestParse_ControlFlow(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"if", "fn main() -> i32 { if (true) { return 1; } return 0; }"},
		{"if_else", "fn main() -> i32 { if (false) { return 1; } else { return 0; } }"},
		{"while", "fn main() -> i32 { i32 i = 0; while (i < 10) { i = i + 1; } return 0; }"},
		{"for", "fn main() -> i32 { for (i32 i = 0; i < 10; i = i + 1) {} return 0; }"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := parseSource(tt.src)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_Expressions(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"binary", "fn f() -> i32 { return 1 + 2 * 3; }"},
		{"unary", "fn f() -> i32 { return -42; }"},
		{"call", "fn f() -> i32 { return g(1, 2); }"},
		{"member", "fn f() -> i32 { return a.b; }"},
		{"index", "fn f() -> i32 { return a[0]; }"},
		{"type_conv", "fn f() -> string { return string(42); }"},
		{"comparison", "fn f() -> bool { return 1 < 2 && 3 > 1; }"},
		{"array_lit", "fn f() { array<i32> a = [1, 2, 3]; }"},
		{"map_lit", `fn f() { map<string,i32> m = {"a": 1}; }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := parseSource(tt.src)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_Imports(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"simple", `import "fmt";`},
		{"alias", `import "fmt" as f;`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			n, err := parseSource(tt.src)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}
			if n != 1 {
				t.Errorf("got %d decls, want 1", n)
			}
		})
	}
}

func TestParse_Classes(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"basic", "class Foo { i32 x; }"},
		{"with_method", "class Foo { i32 x; fn get() -> i32 { return self.x; } }"},
		{"pub", "pub class Bar { string name; }"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := parseSource(tt.src)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_TupleBindings(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"paren_free", `fn foo() -> (i32, err) { return 1; } fn main() -> i32 { i32 n, err e = foo(); return 0; }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := parseSource(tt.src)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_Errors(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantErrSub string
	}{
		{"missing_semi", "i32 x = 5", "expected"},
		{"bad_fn", "fn () {}", "expected"},
		{"unclosed_brace", "fn f() {", "expected"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := parseSource(tt.src)
			if err == nil {
				t.Fatal("expected error, got nil")
			}
			if !strings.Contains(err.Error(), tt.wantErrSub) {
				t.Errorf("error = %q, want substring %q", err.Error(), tt.wantErrSub)
			}
		})
	}
}

func TestParse_Switch(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"basic", `fn f() { switch x { case 1: return; } }`},
		{"multi_case", `fn f() { switch x { case 1, 2: return; case 3: return; } }`},
		{"default", `fn f() { switch x { case 1: return; default: return; } }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_Defer(t *testing.T) {
	src := `fn f() { defer close(); }`
	if _, err := parseSource(src); err != nil {
		t.Fatalf("parse error: %v", err)
	}
}

func TestParse_ForIn(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"array", `fn f() { for v in items { } }`},
		{"map", `fn f() { for k, v in items { } }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_ConstStmt(t *testing.T) {
	src := `fn f() { const i32 MAX = 100; }`
	if _, err := parseSource(src); err != nil {
		t.Fatalf("parse error: %v", err)
	}
}

func TestParse_LocalFn(t *testing.T) {
	src := `fn f() { fn helper() -> i32 { return 1; } }`
	if _, err := parseSource(src); err != nil {
		t.Fatalf("parse error: %v", err)
	}
}

func TestParse_Enum(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"basic", `enum Color { Red, Green, Blue }`},
		{"with_values", `enum Status { Ok = 0, Err = 1 }`},
		{"pub", `pub enum Dir { Up, Down }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_ConstDecl(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"basic", `const i32 MAX = 100;`},
		{"pub", `pub const string NAME = "basl";`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_Interface(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"basic", `interface Stringer { fn to_string() -> string; }`},
		{"multi_method", `interface Shape { fn area() -> f64; fn name() -> string; }`},
		{"with_params", `interface Writer { fn write(string data) -> i32; }`},
		{"pub", `pub interface Reader { fn read() -> string; }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_TupleBindStmt(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"two_values", `fn get() -> (i32, err) { return 1; } fn f() { i32 n, err e = get(); }`},
		{"discard", `fn get() -> (i32, err) { return 1; } fn f() { i32 n, err _ = get(); }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_CompoundAssign(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"plus", `fn f() { i32 x = 0; x += 1; }`},
		{"minus", `fn f() { i32 x = 10; x -= 1; }`},
		{"star", `fn f() { i32 x = 2; x *= 3; }`},
		{"slash", `fn f() { i32 x = 10; x /= 2; }`},
		{"percent", `fn f() { i32 x = 10; x %= 3; }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_IncDec(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"inc", `fn f() { i32 x = 0; x++; }`},
		{"dec", `fn f() { i32 x = 10; x--; }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_FString(t *testing.T) {
	src := "fn f() -> string { string name = \"world\"; return f\"hello {name}\"; }"
	if _, err := parseSource(src); err != nil {
		t.Fatalf("parse error: %v", err)
	}
}

func TestParse_ReturnMultipleValues(t *testing.T) {
	src := `fn f() -> (i32, err) { return (42, err("fail")); }`
	if _, err := parseSource(src); err != nil {
		t.Fatalf("parse error: %v", err)
	}
}

func TestParse_NestedGenerics(t *testing.T) {
	tests := []struct {
		name string
		src  string
	}{
		{"array_of_array", `fn f() { array<array<i32>> a = [[1]]; }`},
		{"map_of_array", `fn f() { map<string, array<i32>> m = {"a": [1]}; }`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := parseSource(tt.src); err != nil {
				t.Fatalf("parse error: %v", err)
			}
		})
	}
}

func TestParse_ErrorCases(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantErrSub string
	}{
		{"switch_no_brace", `fn f() { switch x case 1: return; }`, "expected"},
		{"for_in_no_in", `fn f() { for v items { } }`, "expected"},
		{"interface_no_fn", `interface I { to_string() -> string; }`, "expected"},
		{"enum_no_brace", `enum Color Red, Green`, "expected"},
		{"const_no_assign", `const i32 X;`, "expected"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := parseSource(tt.src)
			if err == nil {
				t.Fatal("expected error, got nil")
			}
			if !strings.Contains(err.Error(), tt.wantErrSub) {
				t.Errorf("error = %q, want substring %q", err.Error(), tt.wantErrSub)
			}
		})
	}
}
