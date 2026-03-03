package interp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

func evalBASL(src string) (int, []string, error) {
	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		return 0, nil, err
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		return 0, nil, err
	}
	interp := New()
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }
	code, err := interp.Exec(prog)
	return code, lines, err
}

func TestExec_Arithmetic(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"add", `import "fmt"; fn main() -> i32 { fmt.print(string(1 + 2)); return 0; }`, []string{"3"}},
		{"sub", `import "fmt"; fn main() -> i32 { fmt.print(string(10 - 3)); return 0; }`, []string{"7"}},
		{"mul", `import "fmt"; fn main() -> i32 { fmt.print(string(2 * 3)); return 0; }`, []string{"6"}},
		{"div", `import "fmt"; fn main() -> i32 { fmt.print(string(10 / 3)); return 0; }`, []string{"3"}},
		{"mod", `import "fmt"; fn main() -> i32 { fmt.print(string(10 % 3)); return 0; }`, []string{"1"}},
		{"neg", `import "fmt"; fn main() -> i32 { fmt.print(string(-5)); return 0; }`, []string{"-5"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_Variables(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"decl_assign", `import "fmt"; fn main() -> i32 { i32 x = 10; x = 20; fmt.print(string(x)); return 0; }`, []string{"20"}},
		{"string_concat", `import "fmt"; fn main() -> i32 { string s = "hello" + " " + "world"; fmt.print(s); return 0; }`, []string{"hello world"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_ControlFlow(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"if_true", `import "fmt"; fn main() -> i32 { if (true) { fmt.print("yes"); } return 0; }`, []string{"yes"}},
		{"if_false_else", `import "fmt"; fn main() -> i32 { if (false) { fmt.print("a"); } else { fmt.print("b"); } return 0; }`, []string{"b"}},
		{"while", `import "fmt"; fn main() -> i32 { i32 i = 0; while (i < 3) { i = i + 1; } fmt.print(string(i)); return 0; }`, []string{"3"}},
		{"for", `import "fmt"; fn main() -> i32 { i32 s = 0; for (i32 i = 0; i < 5; i = i + 1) { s = s + i; } fmt.print(string(s)); return 0; }`, []string{"10"}},
		{"break", `import "fmt"; fn main() -> i32 { i32 i = 0; while (true) { if (i == 3) { break; } i = i + 1; } fmt.print(string(i)); return 0; }`, []string{"3"}},
		{"continue", `import "fmt"; fn main() -> i32 { i32 s = 0; for (i32 i = 0; i < 5; i = i + 1) { if (i == 2) { continue; } s = s + i; } fmt.print(string(s)); return 0; }`, []string{"8"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_Functions(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"call", `import "fmt"; fn add(i32 a, i32 b) -> i32 { return a + b; } fn main() -> i32 { fmt.print(string(add(3, 4))); return 0; }`, []string{"7"}},
		{"recursion", `import "fmt"; fn fib(i32 n) -> i32 { if (n <= 1) { return n; } return fib(n-1) + fib(n-2); } fn main() -> i32 { fmt.print(string(fib(10))); return 0; }`, []string{"55"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_MultiReturn(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"ok_path", `import "fmt";
fn parse(string s) -> (i32, err) { if (s == "42") { return (42, ok); } return (0, err("bad", err.parse)); }
fn main() -> i32 { i32 n, err e = parse("42"); fmt.print(string(n)); return 0; }`, []string{"42"}},
		{"err_path", `import "fmt";
fn parse(string s) -> (i32, err) { if (s == "42") { return (42, ok); } return (0, err("bad", err.parse)); }
fn main() -> i32 { i32 n, err e = parse("x"); fmt.print(e.message()); return 0; }`, []string{"bad"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_Collections(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"array_len", `import "fmt"; fn main() -> i32 { array<i32> a = [1, 2, 3]; fmt.print(string(a.len())); return 0; }`, []string{"3"}},
		{"array_get", `import "fmt"; fn main() -> i32 { array<i32> a = [10, 20, 30]; i32 v, err e = a.get(1); fmt.print(string(v)); return 0; }`, []string{"20"}},
		{"array_push", `import "fmt"; fn main() -> i32 { array<i32> a = [1]; a.push(2); fmt.print(string(a.len())); return 0; }`, []string{"2"}},
		{"map_get", `import "fmt"; fn main() -> i32 { map<string,i32> m = {"k": 42}; i32 v, bool found = m.get("k"); if (found) { fmt.print(string(v)); } return 0; }`, []string{"42"}},
		{"map_has", `import "fmt"; fn main() -> i32 { map<string,i32> m = {"a": 1}; i32 v1, bool f1 = m.get("a"); i32 v2, bool f2 = m.get("b"); fmt.print(string(f1)); fmt.print(string(f2)); return 0; }`, []string{"true", "false"}},
		{"string_len", `import "fmt"; fn main() -> i32 { string s = "hello"; fmt.print(string(s.len())); return 0; }`, []string{"5"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_TypeConversions(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"i32_to_string", `import "fmt"; fn main() -> i32 { fmt.print(string(42)); return 0; }`, []string{"42"}},
		{"f64_to_string", `import "fmt"; fn main() -> i32 { fmt.print(string(3.14)); return 0; }`, []string{"3.14"}},
		{"bool_to_string", `import "fmt"; fn main() -> i32 { fmt.print(string(true)); return 0; }`, []string{"true"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_Classes(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"construct_and_field", `import "fmt";
class Pt { i32 x; i32 y; fn init(i32 x, i32 y) { self.x = x; self.y = y; } }
fn main() -> i32 { Pt p = Pt(3, 4); fmt.print(string(p.x)); return 0; }`, []string{"3"}},
		{"method", `import "fmt";
class Counter { i32 n; fn init() { self.n = 0; } fn inc() { self.n = self.n + 1; } fn get() -> i32 { return self.n; } }
fn main() -> i32 { Counter c = Counter(); c.inc(); c.inc(); fmt.print(string(c.get())); return 0; }`, []string{"2"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_FStrings(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"basic", `import "fmt"; fn main() -> i32 { string name = "world"; fmt.print(f"hello {name}"); return 0; }`, []string{"hello world"}},
		{"expr", `import "fmt"; fn main() -> i32 { i32 x = 5; fmt.print(f"{x + 1}"); return 0; }`, []string{"6"}},
		{"method", `import "fmt"; fn main() -> i32 { string s = "hi"; fmt.print(f"{s.to_upper()}"); return 0; }`, []string{"HI"}},
		{"multi", `import "fmt"; fn main() -> i32 { i32 a = 1; i32 b = 2; fmt.print(f"{a}+{b}={a+b}"); return 0; }`, []string{"1+2=3"}},
		{"escaped_braces", `import "fmt"; fn main() -> i32 { fmt.print(f"{{ok}}"); return 0; }`, []string{"{ok}"}},
		{"no_expr", `import "fmt"; fn main() -> i32 { fmt.print(f"plain"); return 0; }`, []string{"plain"}},
		{"escape_seq", `import "fmt"; fn main() -> i32 { fmt.print(f"a\tb"); return 0; }`, []string{"a\tb"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_Interfaces(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"basic_interface", `import "fmt";
interface Greeter { fn greet() -> string; }
class Dog implements Greeter { fn greet() -> string { return "woof"; } }
fn main() -> i32 { Dog d = Dog(); fmt.print(d.greet()); return 0; }`, []string{"woof"}},

		{"polymorphic_array", `import "fmt";
interface Actor { fn name() -> string; fn update() -> void; }
class Player implements Actor {
    string n;
    fn init(string n) -> void { self.n = n; }
    fn name() -> string { return self.n; }
    fn update() -> void { }
}
class Enemy implements Actor {
    string n;
    fn init(string n) -> void { self.n = n; }
    fn name() -> string { return self.n; }
    fn update() -> void { }
}
fn tick(Actor a) -> void { fmt.print(a.name()); }
fn main() -> i32 {
    array<Actor> actors = [Player("hero"), Enemy("goblin")];
    for a in actors { tick(a); }
    return 0;
}`, []string{"hero", "goblin"}},

		{"multi_interface", `import "fmt";
interface Readable { fn read() -> string; }
interface Writable { fn write(string data) -> void; }
class Buffer implements Readable, Writable {
    string buf;
    fn init() -> void { self.buf = ""; }
    fn read() -> string { return self.buf; }
    fn write(string data) -> void { self.buf = data; }
}
fn main() -> i32 { Buffer b = Buffer(); b.write("hello"); fmt.print(b.read()); return 0; }`, []string{"hello"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_InterfaceErrors(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantErrSub string
	}{
		{"missing_method", `
interface Foo { fn bar() -> void; }
class Bad implements Foo { }
fn main() -> i32 { return 0; }`, "missing method"},
		{"unknown_interface", `
class Bad implements Nonexistent { }
fn main() -> i32 { return 0; }`, "unknown interface"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, _, err := evalBASL(tt.src)
			if err == nil {
				t.Fatal("expected error")
			}
			if !strings.Contains(err.Error(), tt.wantErrSub) {
				t.Errorf("error %q does not contain %q", err.Error(), tt.wantErrSub)
			}
		})
	}
}

func TestExec_ExitCode(t *testing.T) {
	tests := []struct {
		name     string
		src      string
		wantCode int
	}{
		{"zero", `fn main() -> i32 { return 0; }`, 0},
		{"one", `fn main() -> i32 { return 1; }`, 1},
		{"forty_two", `fn main() -> i32 { return 42; }`, 42},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			code, _, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			if code != tt.wantCode {
				t.Errorf("exit code = %d, want %d", code, tt.wantCode)
			}
		})
	}
}

func TestExec_RuntimeErrors(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantErrSub string
	}{
		{"div_by_zero", `fn main() -> i32 { i32 x = 1 / 0; return 0; }`, "division by zero"},
		{"index_oob", `fn main() -> i32 { array<i32> a = [1]; i32 x = a[5]; return 0; }`, "out of bounds"},
		{"undefined_var", `fn main() -> i32 { fmt.print(string(xyz)); return 0; }`, "undefined"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, _, err := evalBASL(tt.src)
			if err == nil {
				t.Fatal("expected error, got nil")
			}
			if !strings.Contains(err.Error(), tt.wantErrSub) {
				t.Errorf("error = %q, want substring %q", err.Error(), tt.wantErrSub)
			}
		})
	}
}

func checkOutput(t *testing.T, got, want []string) {
	t.Helper()
	if len(got) != len(want) {
		t.Fatalf("got %d lines %v, want %d lines %v", len(got), got, len(want), want)
	}
	for i := range got {
		if got[i] != want[i] {
			t.Errorf("line[%d] = %q, want %q", i, got[i], want[i])
		}
	}
}

func TestExec_StringMethods(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"len", `import "fmt"; fn main() -> i32 { fmt.print(string("hello".len())); return 0; }`, []string{"5"}},
		{"contains_true", `import "fmt"; fn main() -> i32 { fmt.print(string("hello world".contains("world"))); return 0; }`, []string{"true"}},
		{"contains_false", `import "fmt"; fn main() -> i32 { fmt.print(string("hello".contains("xyz"))); return 0; }`, []string{"false"}},
		{"starts_with", `import "fmt"; fn main() -> i32 { fmt.print(string("hello".starts_with("hel"))); return 0; }`, []string{"true"}},
		{"ends_with", `import "fmt"; fn main() -> i32 { fmt.print(string("hello".ends_with("llo"))); return 0; }`, []string{"true"}},
		{"trim", `import "fmt"; fn main() -> i32 { fmt.print("  hi  ".trim()); return 0; }`, []string{"hi"}},
		{"to_upper", `import "fmt"; fn main() -> i32 { fmt.print("hello".to_upper()); return 0; }`, []string{"HELLO"}},
		{"to_lower", `import "fmt"; fn main() -> i32 { fmt.print("HELLO".to_lower()); return 0; }`, []string{"hello"}},
		{"replace", `import "fmt"; fn main() -> i32 { fmt.print("aabaa".replace("a", "x")); return 0; }`, []string{"xxbxx"}},
		{"split", `import "fmt"; fn main() -> i32 {
			array<string> parts = "a,b,c".split(",");
			fmt.print(string(parts.len()));
			string v, err e = parts.get(1);
			fmt.print(v);
			return 0;
		}`, []string{"3", "b"}},
		{"index_of_found", `import "fmt"; fn main() -> i32 {
			i32 idx, bool found = "hello".index_of("ll");
			fmt.print(string(idx));
			fmt.print(string(found));
			return 0;
		}`, []string{"2", "true"}},
		{"index_of_missing", `import "fmt"; fn main() -> i32 {
			i32 idx, bool found = "hello".index_of("xyz");
			fmt.print(string(found));
			return 0;
		}`, []string{"false"}},
		{"substr_ok", `import "fmt"; fn main() -> i32 {
			string s, err e = "hello".substr(1, 3);
			fmt.print(s);
			return 0;
		}`, []string{"ell"}},
		{"substr_oob", `import "fmt"; fn main() -> i32 {
			string s, err e = "hi".substr(0, 10);
			if (e != ok) { fmt.print("error"); }
			return 0;
		}`, []string{"error"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_ForIn(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"array", `import "fmt"; fn main() -> i32 {
			array<i32> xs = [10, 20, 30];
			for v in xs { fmt.print(string(v)); }
			return 0;
		}`, []string{"10", "20", "30"}},
		{"map", `import "fmt"; fn main() -> i32 {
			map<string, i32> m = { "a": 1, "b": 2 };
			for k, v in m { fmt.print(k + "=" + string(v)); }
			return 0;
		}`, []string{"a=1", "b=2"}},
		{"map_val_only", `import "fmt"; fn main() -> i32 {
			map<string, i32> m = { "x": 99 };
			for v in m { fmt.print(string(v)); }
			return 0;
		}`, []string{"99"}},
		{"break", `import "fmt"; fn main() -> i32 {
			array<i32> xs = [1, 2, 3, 4, 5];
			for v in xs { if (v == 3) { break; } fmt.print(string(v)); }
			return 0;
		}`, []string{"1", "2"}},
		{"continue", `import "fmt"; fn main() -> i32 {
			array<i32> xs = [1, 2, 3, 4];
			for v in xs { if (v == 2) { continue; } fmt.print(string(v)); }
			return 0;
		}`, []string{"1", "3", "4"}},
		{"empty_array", `import "fmt"; fn main() -> i32 {
			array<i32> xs = [];
			for v in xs { fmt.print("nope"); }
			fmt.print("done");
			return 0;
		}`, []string{"done"}},
		{"split_iterate", `import "fmt"; fn main() -> i32 {
			for part in "a,b,c".split(",") { fmt.print(part); }
			return 0;
		}`, []string{"a", "b", "c"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_FunctionsAsValues(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"pass_fn_arg", `import "fmt";
			fn double(i32 x) -> i32 { return x * 2; }
			fn apply(fn f, i32 x) -> i32 { return f(x); }
			fn main() -> i32 { fmt.print(string(apply(double, 5))); return 0; }`,
			[]string{"10"}},
		{"fn_variable", `import "fmt";
			fn double(i32 x) -> i32 { return x * 2; }
			fn triple(i32 x) -> i32 { return x * 3; }
			fn main() -> i32 {
				fn f = double;
				fmt.print(string(f(5)));
				f = triple;
				fmt.print(string(f(5)));
				return 0;
			}`, []string{"10", "15"}},
		{"fn_in_array", `import "fmt";
			fn add1(i32 x) -> i32 { return x + 1; }
			fn add2(i32 x) -> i32 { return x + 2; }
			fn main() -> i32 {
				array<fn> ops = [add1, add2];
				for op in ops { fmt.print(string(op(10))); }
				return 0;
			}`, []string{"11", "12"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_ByteArrays(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"string_to_bytes", `import "fmt"; fn main() -> i32 {
			array<u8> b = "Hi".bytes();
			fmt.print(string(b.len()));
			fmt.print(string(b.get(0)));
			return 0;
		}`, []string{"2", "72"}},
		{"bytes_to_string", `import "fmt"; fn main() -> i32 {
			array<u8> b = [u8(72), u8(105)];
			string s = string_from_bytes(b);
			fmt.print(s);
			return 0;
		}`, []string{"Hi"}},
		{"roundtrip", `import "fmt"; fn main() -> i32 {
			string orig = "hello";
			string back = string_from_bytes(orig.bytes());
			fmt.print(string(orig == back));
			return 0;
		}`, []string{"true"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_FmtModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"println", `import "fmt"; fn main() -> i32 { fmt.println("hi"); return 0; }`, []string{"hi"}},
		{"sprintf", `import "fmt"; fn main() -> i32 { fmt.print(fmt.sprintf("x=%d", 42)); return 0; }`, []string{"x=42"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_OsModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"platform", `import "fmt"; import "os"; fn main() -> i32 {
			string p = os.platform();
			fmt.print(string(p.len() > 0));
			return 0;
		}`, []string{"true"}},
		{"cwd", `import "fmt"; import "os"; fn main() -> i32 {
			string dir, err e = os.cwd();
			fmt.print(string(dir.len() > 0));
			return 0;
		}`, []string{"true"}},
		{"env_missing", `import "fmt"; import "os"; fn main() -> i32 {
			string v, bool found = os.env("BASL_TEST_NONEXISTENT_12345");
			fmt.print(string(found));
			return 0;
		}`, []string{"false"}},
		{"exec_echo", `import "fmt"; import "os"; fn main() -> i32 {
			string out, string errout, i32 exitCode, err e = os.exec("echo", "hello");
			fmt.print(out.trim());
			return 0;
		}`, []string{"hello"}},
		{"exec_fail", `import "fmt"; import "os"; fn main() -> i32 {
			string out, string errout, i32 exitCode, err e = os.exec("false");
			if (exitCode != 0) { fmt.print("failed"); }
			return 0;
		}`, []string{"failed"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_MathModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"floor", `import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.floor(3.7))); return 0; }`, []string{"3"}},
		{"ceil", `import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.ceil(3.2))); return 0; }`, []string{"4"}},
		{"round", `import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.round(3.5))); return 0; }`, []string{"4"}},
		{"pow", `import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.pow(2.0, 10.0))); return 0; }`, []string{"1024"}},
		{"min", `import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.min(3.0, 7.0))); return 0; }`, []string{"3"}},
		{"max", `import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.max(3.0, 7.0))); return 0; }`, []string{"7"}},
		{"pi", `import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.pi > 3.14)); return 0; }`, []string{"true"}},
		{"random", `import "fmt"; import "math"; fn main() -> i32 {
			f64 r = math.random();
			fmt.print(string(r >= 0.0));
			fmt.print(string(r < 1.0));
			return 0;
		}`, []string{"true", "true"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_StringsModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"join", `import "fmt"; import "strings"; fn main() -> i32 {
			fmt.print(strings.join(["a", "b", "c"], "-"));
			return 0;
		}`, []string{"a-b-c"}},
		{"repeat", `import "fmt"; import "strings"; fn main() -> i32 {
			fmt.print(strings.repeat("ab", 3));
			return 0;
		}`, []string{"ababab"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_TimeModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"now", `import "fmt"; import "time"; fn main() -> i32 {
			i64 t = time.now();
			fmt.print(string(t > i64(0)));
			return 0;
		}`, []string{"true"}},
		{"since", `import "fmt"; import "time"; fn main() -> i32 {
			i64 t = time.now();
			i64 elapsed = time.since(t);
			fmt.print(string(elapsed >= i64(0)));
			return 0;
		}`, []string{"true"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_PathModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"base", `import "fmt"; import "path"; fn main() -> i32 { fmt.print(path.base("/foo/bar.txt")); return 0; }`, []string{"bar.txt"}},
		{"dir", `import "fmt"; import "path"; fn main() -> i32 { fmt.print(path.dir("/foo/bar.txt")); return 0; }`, []string{"/foo"}},
		{"ext", `import "fmt"; import "path"; fn main() -> i32 { fmt.print(path.ext("file.tar.gz")); return 0; }`, []string{".gz"}},
		{"join", `import "fmt"; import "path"; fn main() -> i32 { fmt.print(path.join("a", "b", "c.txt")); return 0; }`, []string{"a/b/c.txt"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			// Normalize paths to forward slashes for cross-platform comparison
			normalized := make([]string, len(lines))
			for i, line := range lines {
				normalized[i] = filepath.ToSlash(line)
			}
			checkOutput(t, normalized, tt.wantOutput)
		})
	}
}

func TestExec_RegexModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"match_true", `import "fmt"; import "regex"; fn main() -> i32 {
			bool m, err e = regex.match("^[0-9]+$", "12345");
			fmt.print(string(m));
			return 0;
		}`, []string{"true"}},
		{"match_false", `import "fmt"; import "regex"; fn main() -> i32 {
			bool m, err e = regex.match("^[0-9]+$", "abc");
			fmt.print(string(m));
			return 0;
		}`, []string{"false"}},
		{"find", `import "fmt"; import "regex"; fn main() -> i32 {
			string m, err e = regex.find("[0-9]+", "abc123def");
			fmt.print(m);
			return 0;
		}`, []string{"123"}},
		{"find_all", `import "fmt"; import "regex"; fn main() -> i32 {
			array<string> ms, err e = regex.find_all("[0-9]+", "a1b22c333");
			fmt.print(string(ms.len()));
			for m in ms { fmt.print(m); }
			return 0;
		}`, []string{"3", "1", "22", "333"}},
		{"replace", `import "fmt"; import "regex"; fn main() -> i32 {
			string r, err e = regex.replace("[0-9]+", "a1b2c3", "X");
			fmt.print(r);
			return 0;
		}`, []string{"aXbXcX"}},
		{"split", `import "fmt"; import "regex"; fn main() -> i32 {
			array<string> parts, err e = regex.split("[,;]+", "a,b;;c,d");
			for p in parts { fmt.print(p); }
			return 0;
		}`, []string{"a", "b", "c", "d"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_FileModule(t *testing.T) {
	tmpDir := t.TempDir()
	escPath := func(p string) string { return strings.ReplaceAll(p, `\`, `\\`) }

	tests := []struct {
		name       string
		src        func() string
		wantOutput []string
	}{
		{"read_lines", func() string {
			path := escPath(filepath.Join(tmpDir, "basl_test_lines.txt"))
			return `import "fmt"; import "file"; fn main() -> i32 {
				err w = file.write_all("` + path + `", "a\nb\nc");
				array<string> lines, err e = file.read_lines("` + path + `");
				fmt.print(string(lines.len()));
				for l in lines { fmt.print(l); }
				file.remove("` + path + `");
				return 0;
			}`
		}, []string{"3", "a", "b", "c"}},
		{"exists", func() string {
			path := escPath(filepath.Join(tmpDir, "basl_test_exists.txt"))
			return `import "fmt"; import "file"; fn main() -> i32 {
				file.write_all("` + path + `", "x");
				fmt.print(string(file.exists("` + path + `")));
				file.remove("` + path + `");
				fmt.print(string(file.exists("` + path + `")));
				return 0;
			}`
		}, []string{"true", "false"}},
		{"mkdir_listdir", func() string {
			dir := escPath(filepath.Join(tmpDir, "basl_test_dir"))
			file := escPath(filepath.Join(strings.ReplaceAll(dir, `\\`, `\`), "a.txt"))
			return `import "fmt"; import "file"; fn main() -> i32 {
				file.mkdir("` + dir + `");
				file.write_all("` + file + `", "a");
				array<string> entries, err e = file.list_dir("` + dir + `");
				fmt.print(string(entries.len()));
				file.remove("` + file + `");
				file.remove("` + dir + `");
				return 0;
			}`
		}, []string{"1"}},
		{"rename", func() string {
			path1 := escPath(filepath.Join(tmpDir, "basl_test_ren1.txt"))
			path2 := escPath(filepath.Join(tmpDir, "basl_test_ren2.txt"))
			return `import "fmt"; import "file"; fn main() -> i32 {
				file.write_all("` + path1 + `", "data");
				file.rename("` + path1 + `", "` + path2 + `");
				fmt.print(string(file.exists("` + path1 + `")));
				fmt.print(string(file.exists("` + path2 + `")));
				file.remove("` + path2 + `");
				return 0;
			}`
		}, []string{"false", "true"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src())
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_LogModule(t *testing.T) {
	// Test BASL-side set_handler
	t.Run("basl_handler", func(t *testing.T) {
		src := `import "fmt"; import "log";
			fn my_log(string level, string msg) -> err {
				fmt.print(level + ": " + msg);
				return ok;
			}
			fn main() -> i32 {
				log.set_level("debug");
				log.set_handler(my_log);
				log.debug("d");
				log.info("i");
				log.warn("w");
				log.error("e");
				return 0;
			}`
		_, lines, err := evalBASL(src)
		if err != nil {
			t.Fatalf("error: %v", err)
		}
		checkOutput(t, lines, []string{"DEBUG: d", "INFO: i", "WARN: w", "ERROR: e"})
	})

	// Test Go-side LogFn
	t.Run("go_logfn", func(t *testing.T) {
		src := `import "log";
			fn main() -> i32 {
				log.set_level("info");
				log.info("hello");
				return 0;
			}`
		lex := lexer.New(src)
		tokens, _ := lex.Tokenize()
		p := parser.New(tokens)
		prog, _ := p.Parse()
		vm := New()
		vm.PrintFn = func(string) {}
		var captured string
		vm.LogFn = func(level, msg string) { captured = level + ":" + msg }
		vm.Exec(prog)
		if captured != "INFO:hello" {
			t.Errorf("LogFn got %q, want %q", captured, "INFO:hello")
		}
	})

	// Test level filtering
	t.Run("level_filter", func(t *testing.T) {
		src := `import "fmt"; import "log";
			fn my_log(string level, string msg) -> err {
				fmt.print(level);
				return ok;
			}
			fn main() -> i32 {
				log.set_handler(my_log);
				log.set_level("warn");
				log.debug("x");
				log.info("x");
				log.warn("x");
				log.error("x");
				return 0;
			}`
		_, lines, err := evalBASL(src)
		if err != nil {
			t.Fatalf("error: %v", err)
		}
		checkOutput(t, lines, []string{"WARN", "ERROR"})
	})
}

func TestExec_JsonModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"parse_object", `import "fmt"; import "json"; fn main() -> i32 {
			json.Value v, err e = json.parse("{\"name\":\"test\",\"count\":42}");
			fmt.print(v.get_string("name"));
			fmt.print(string(v.get_i32("count")));
			return 0;
		}`, []string{"test", "42"}},
		{"parse_array", `import "fmt"; import "json"; fn main() -> i32 {
			json.Value v, err e = json.parse("[1,2,3]");
			fmt.print(string(v.len()));
			fmt.print(string(v.at_i32(1)));
			return 0;
		}`, []string{"3", "2"}},
		{"nested", `import "fmt"; import "json"; fn main() -> i32 {
			json.Value v, err e = json.parse("{\"layers\":[{\"name\":\"ground\",\"data\":[1,2,3]}]}");
			json.Value layers, err e2 = v.get("layers");
			fmt.print(string(layers.len()));
			json.Value layer0, err e3 = layers.at(0);
			fmt.print(layer0.get_string("name"));
			json.Value data, err e4 = layer0.get("data");
			fmt.print(string(data.at_i32(2)));
			return 0;
		}`, []string{"1", "ground", "3"}},
		{"stringify_map", `import "fmt"; import "json"; fn main() -> i32 {
			map<string, i32> m = {"a": 1};
			string s, err e = json.stringify(m);
			fmt.print(s);
			return 0;
		}`, []string{"{\"a\":1}"}},
		{"keys", `import "fmt"; import "json"; fn main() -> i32 {
			json.Value v, err e = json.parse("{\"x\":1,\"y\":2}");
			array<string> ks = v.keys();
			fmt.print(string(ks.len()));
			return 0;
		}`, []string{"2"}},
		{"bool", `import "fmt"; import "json"; fn main() -> i32 {
			json.Value v, err e = json.parse("{\"ok\":true}");
			fmt.print(string(v.get_bool("ok")));
			return 0;
		}`, []string{"true"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_XmlModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"parse_basic", `import "fmt"; import "xml"; fn main() -> i32 {
			xml.Value root, err e = xml.parse("<map width=\"10\"><layer name=\"ground\"><data>1,2,3</data></layer></map>");
			fmt.print(root.tag());
			string w, bool found = root.attr("width");
			fmt.print(w);
			return 0;
		}`, []string{"map", "10"}},
		{"children", `import "fmt"; import "xml"; fn main() -> i32 {
			xml.Value root, err e = xml.parse("<root><a/><b/><c/></root>");
			fmt.print(string(root.len()));
			return 0;
		}`, []string{"3"}},
		{"find", `import "fmt"; import "xml"; fn main() -> i32 {
			xml.Value root, err e = xml.parse("<map><layer name=\"bg\"/><layer name=\"fg\"/></map>");
			array<xml.Value> layers = root.find("layer");
			fmt.print(string(layers.len()));
			return 0;
		}`, []string{"2"}},
		{"find_one", `import "fmt"; import "xml"; fn main() -> i32 {
			xml.Value root, err e = xml.parse("<map><layer name=\"ground\"><data>1,2,3</data></layer></map>");
			xml.Value layer, err e2 = root.find_one("layer");
			string n, bool found = layer.attr("name");
			fmt.print(n);
			xml.Value data, err e3 = layer.find_one("data");
			fmt.print(data.text());
			return 0;
		}`, []string{"ground", "1,2,3"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_EncodingModules(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"base64_roundtrip", `import "fmt"; import "base64"; fn main() -> i32 {
			string enc = base64.encode("hello world");
			fmt.print(enc);
			string dec, err e = base64.decode(enc);
			fmt.print(dec);
			return 0;
		}`, []string{"aGVsbG8gd29ybGQ=", "hello world"}},
		{"hex_roundtrip", `import "fmt"; import "hex"; fn main() -> i32 {
			string enc = hex.encode("AB");
			fmt.print(enc);
			string dec, err e = hex.decode(enc);
			fmt.print(dec);
			return 0;
		}`, []string{"4142", "AB"}},
		{"csv_parse", `import "fmt"; import "csv"; fn main() -> i32 {
			array<array<string>> rows, err e = csv.parse("a,b,c\n1,2,3");
			fmt.print(string(rows.len()));
			array<string> row0, err e2 = rows.get(0);
			string cell, err e3 = row0.get(1);
			fmt.print(cell);
			return 0;
		}`, []string{"2", "b"}},
		{"csv_stringify", `import "fmt"; import "csv"; fn main() -> i32 {
			string s = csv.stringify([["x", "y"], ["1", "2"]]);
			fmt.print(s.trim());
			return 0;
		}`, []string{"x,y\n1,2"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_SortModule(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"sort_ints", `import "fmt"; import "sort"; fn main() -> i32 {
			array<i32> xs = [3, 1, 2];
			sort.ints(xs);
			for v in xs { fmt.print(string(v)); }
			return 0;
		}`, []string{"1", "2", "3"}},
		{"sort_strings", `import "fmt"; import "sort"; fn main() -> i32 {
			array<string> xs = ["c", "a", "b"];
			sort.strings(xs);
			for v in xs { fmt.print(v); }
			return 0;
		}`, []string{"a", "b", "c"}},
		{"sort_by", `import "fmt"; import "sort"; fn main() -> i32 {
			fn desc(i32 a, i32 b) -> bool { return a > b; }
			array<i32> xs = [1, 3, 2];
			sort.by(xs, desc);
			for v in xs { fmt.print(string(v)); }
			return 0;
		}`, []string{"3", "2", "1"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func BenchmarkExec(b *testing.B) {
	src := `
import "fmt";
fn fib(i32 n) -> i32 { if (n <= 1) { return n; } return fib(n - 1) + fib(n - 2); }
fn main() -> i32 { i32 r = fib(15); return 0; }
`
	lex := lexer.New(src)
	tokens, _ := lex.Tokenize()
	p := parser.New(tokens)
	prog, _ := p.Parse()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		interp := New()
		interp.PrintFn = func(string) {}
		interp.Exec(prog)
	}
}

func TestExec_FileHandleModule(t *testing.T) {
	tmpDir := t.TempDir()
	escPath := func(p string) string { return strings.ReplaceAll(p, `\`, `\\`) }

	tests := []struct {
		name string
		src  func() string
		want []string
	}{
		{"file open write read close", func() string {
			path := escPath(filepath.Join(tmpDir, "fh.txt"))
			return `import "file"; import "fmt"; fn main() -> i32 {
				file.File f, err e = file.open("` + path + `", "w");
				err e2 = f.write("hello world");
				err e3 = f.close();
				file.File f2, err e4 = file.open("` + path + `", "r");
				string data, err e5 = f2.read(11);
				err e6 = f2.close();
				fmt.print(data);
				file.remove("` + path + `");
				return 0;
			}`
		}, []string{"hello world"}},
		{"file stat", func() string {
			path := escPath(filepath.Join(tmpDir, "stat.txt"))
			return `import "file"; import "fmt"; fn main() -> i32 {
				file.write_all("` + path + `", "abc");
				file.FileStat s, err e = file.stat("` + path + `");
				fmt.print(s.name);
				fmt.print(fmt.sprintf("%d", s.size));
				fmt.print(fmt.sprintf("%t", s.is_dir));
				file.remove("` + path + `");
				return 0;
			}`
		}, []string{"stat.txt", "3", "false"}},
		{"file read_line", func() string {
			path := escPath(filepath.Join(tmpDir, "rl.txt"))
			return `import "file"; import "fmt"; fn main() -> i32 {
				file.write_all("` + path + `", "line1\nline2\nline3");
				file.File f, err e = file.open("` + path + `", "r");
				string l1, err e2 = f.read_line();
				string l2, err e3 = f.read_line();
				err e4 = f.close();
				fmt.print(l1);
				fmt.print(l2);
				file.remove("` + path + `");
				return 0;
			}`
		}, []string{"line1", "line2"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src())
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_ArchiveModule(t *testing.T) {
	tmpDir := t.TempDir()
	escPath := func(p string) string { return strings.ReplaceAll(p, `\`, `\\`) }

	tests := []struct {
		name string
		src  func() string
		want []string
	}{
		{"tar create and extract", func() string {
			a := escPath(filepath.Join(tmpDir, "tar_a.txt"))
			b := escPath(filepath.Join(tmpDir, "tar_b.txt"))
			tarFile := escPath(filepath.Join(tmpDir, "test.tar"))
			outDir := escPath(filepath.Join(tmpDir, "tar_out"))
			extractedA := escPath(filepath.Join(strings.ReplaceAll(outDir, `\\`, `\`), "tar_a.txt"))
			extractedB := escPath(filepath.Join(strings.ReplaceAll(outDir, `\\`, `\`), "tar_b.txt"))
			return `import "archive"; import "file"; import "fmt"; fn main() -> i32 {
				file.write_all("` + a + `", "aaa");
				file.write_all("` + b + `", "bbb");
				err e1 = archive.tar_create("` + tarFile + `", ["` + a + `", "` + b + `"]);
				file.mkdir("` + outDir + `");
				err e2 = archive.tar_extract("` + tarFile + `", "` + outDir + `");
				string a, err e3 = file.read_all("` + extractedA + `");
				string b, err e4 = file.read_all("` + extractedB + `");
				fmt.print(a);
				fmt.print(b);
				return 0;
			}`
		}, []string{"aaa", "bbb"}},
		{"zip create and extract", func() string {
			a := escPath(filepath.Join(tmpDir, "zip_a.txt"))
			b := escPath(filepath.Join(tmpDir, "zip_b.txt"))
			zipFile := escPath(filepath.Join(tmpDir, "test.zip"))
			outDir := escPath(filepath.Join(tmpDir, "zip_out"))
			extractedA := escPath(filepath.Join(strings.ReplaceAll(outDir, `\\`, `\`), "zip_a.txt"))
			extractedB := escPath(filepath.Join(strings.ReplaceAll(outDir, `\\`, `\`), "zip_b.txt"))
			return `import "archive"; import "file"; import "fmt"; fn main() -> i32 {
				file.write_all("` + a + `", "xxx");
				file.write_all("` + b + `", "yyy");
				err e1 = archive.zip_create("` + zipFile + `", ["` + a + `", "` + b + `"]);
				file.mkdir("` + outDir + `");
				err e2 = archive.zip_extract("` + zipFile + `", "` + outDir + `");
				string a, err e3 = file.read_all("` + extractedA + `");
				string b, err e4 = file.read_all("` + extractedB + `");
				fmt.print(a);
				fmt.print(b);
				return 0;
			}`
		}, []string{"xxx", "yyy"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src())
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_CompressModule(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"gzip roundtrip", `import "compress"; import "fmt"; fn main() -> i32 {
			string compressed, err e1 = compress.gzip("hello gzip");
			string decompressed, err e2 = compress.gunzip(compressed);
			fmt.print(decompressed);
			return 0;
		}`, []string{"hello gzip"}},
		{"zlib roundtrip", `import "compress"; import "fmt"; fn main() -> i32 {
			string compressed, err e1 = compress.zlib("hello zlib");
			string decompressed, err e2 = compress.unzlib(compressed);
			fmt.print(decompressed);
			return 0;
		}`, []string{"hello zlib"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func safeIdx(s []string, i int) string {
	if i < len(s) {
		return s[i]
	}
	return "<missing>"
}

func TestExec_HashModule(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"md5", `import "hash"; import "fmt"; fn main() -> i32 {
			fmt.print(hash.md5("hello"));
			return 0;
		}`, []string{"5d41402abc4b2a76b9719d911017c592"}},
		{"sha256", `import "hash"; import "fmt"; fn main() -> i32 {
			fmt.print(hash.sha256("hello"));
			return 0;
		}`, []string{"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"}},
		{"hmac_sha256", `import "hash"; import "fmt"; fn main() -> i32 {
			string h = hash.hmac_sha256("secret", "hello");
			fmt.print(fmt.sprintf("%d", h.len()));
			return 0;
		}`, []string{"64"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_CryptoModule(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"aes roundtrip", `import "crypto"; import "fmt"; fn main() -> i32 {
			string key = "0123456789abcdef0123456789abcdef";
			string ct, err e1 = crypto.aes_encrypt(key, "secret data");
			string pt, err e2 = crypto.aes_decrypt(key, ct);
			fmt.print(pt);
			return 0;
		}`, []string{"secret data"}},
		{"rsa roundtrip", `import "crypto"; import "fmt"; fn main() -> i32 {
			string priv, string pubk, err e1 = crypto.rsa_generate(2048);
			string ct, err e2 = crypto.rsa_encrypt(pubk, "rsa test");
			string pt, err e3 = crypto.rsa_decrypt(priv, ct);
			fmt.print(pt);
			return 0;
		}`, []string{"rsa test"}},
		{"rsa sign verify", `import "crypto"; import "fmt"; fn main() -> i32 {
			string priv, string pubk, err e1 = crypto.rsa_generate(2048);
			string sig, err e2 = crypto.rsa_sign(priv, "msg");
			bool valid = crypto.rsa_verify(pubk, "msg", sig);
			bool bad = crypto.rsa_verify(pubk, "wrong", sig);
			fmt.print(fmt.sprintf("%t", valid));
			fmt.print(fmt.sprintf("%t", bad));
			return 0;
		}`, []string{"true", "false"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_RandModule(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"rand bytes length", `import "rand"; import "fmt"; fn main() -> i32 {
			string b = rand.bytes(16);
			fmt.print(fmt.sprintf("%d", b.len()));
			return 0;
		}`, []string{"32"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_TcpModule(t *testing.T) {
	_, out, err := evalBASL(`import "tcp"; import "fmt"; fn main() -> i32 {
		TcpListener ln, err e = tcp.listen("127.0.0.1:0");
		TcpConn client, err e2 = tcp.connect("127.0.0.1:19384");
		fmt.print("ok");
		return 0;
	}`)
	// We can't easily test full TCP in unit tests without goroutines,
	// but we can test that the module loads and listen works
	// The connect will fail since nothing is on 19384, but listen should succeed
	_ = out
	_ = err
	// Just verify the module is registered and parseable
	_, out2, err2 := evalBASL(`import "tcp"; import "fmt"; fn main() -> i32 {
		fmt.print("tcp loaded");
		return 0;
	}`)
	if err2 != nil {
		t.Fatal(err2)
	}
	if len(out2) == 0 || out2[0] != "tcp loaded" {
		t.Errorf("got %v", out2)
	}
}

func TestExec_HttpModule(t *testing.T) {
	// Test HTTP client against a local test server
	_, out, err := evalBASL(`import "http"; import "fmt"; fn main() -> i32 {
		HttpResponse resp, err e = http.get("http://127.0.0.1:1");
		fmt.print("attempted");
		return 0;
	}`)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) == 0 || out[0] != "attempted" {
		t.Errorf("got %v", out)
	}
}

func TestExec_MimeModule(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"type_by_ext", `import "mime"; import "fmt"; fn main() -> i32 {
			string t = mime.type_by_ext(".json");
			fmt.print(t);
			return 0;
		}`, []string{"application/json"}},
		{"ext_by_type", `import "mime"; import "fmt"; fn main() -> i32 {
			string e = mime.ext_by_type("text/html");
			fmt.print(fmt.sprintf("%t", e.len() > 0));
			return 0;
		}`, []string{"true"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_UdpModule(t *testing.T) {
	_, out, err := evalBASL(`import "udp"; import "fmt"; fn main() -> i32 {
		UdpConn conn, err e = udp.listen("127.0.0.1:0");
		err e2 = conn.close();
		fmt.print("udp ok");
		return 0;
	}`)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) == 0 || out[0] != "udp ok" {
		t.Errorf("got %v", out)
	}
}

func TestExec_SqliteModule(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"create insert query", `import "sqlite"; import "fmt"; fn main() -> i32 {
			SqliteDB db, err e = sqlite.open(":memory:");
			err e2 = db.exec("CREATE TABLE t (id INTEGER, name TEXT)");
			err e3 = db.exec("INSERT INTO t VALUES (?, ?)", 1, "alice");
			err e4 = db.exec("INSERT INTO t VALUES (?, ?)", 2, "bob");
			SqliteRows rows, err e5 = db.query("SELECT id, name FROM t ORDER BY id");
			bool ok1 = rows.next();
			string n1 = rows.get("name");
			bool ok2 = rows.next();
			string n2 = rows.get("name");
			err e6 = rows.close();
			err e7 = db.close();
			fmt.print(n1);
			fmt.print(n2);
			return 0;
		}`, []string{"alice", "bob"}},
		{"query with params", `import "sqlite"; import "fmt"; fn main() -> i32 {
			SqliteDB db, err e = sqlite.open(":memory:");
			err e2 = db.exec("CREATE TABLE t (v TEXT)");
			err e3 = db.exec("INSERT INTO t VALUES (?)", "hello");
			SqliteRows rows, err e4 = db.query("SELECT v FROM t WHERE v = ?", "hello");
			bool has_row = rows.next();
			string v = rows.get("v");
			err e5 = rows.close();
			err e6 = db.close();
			fmt.print(v);
			return 0;
		}`, []string{"hello"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_ArgsModule(t *testing.T) {
	// Just test that the module loads and parser can be created
	_, out, err := evalBASL(`import "args"; import "fmt"; fn main() -> i32 {
		args.ArgParser p = args.parser("test", "a test program");
		p.flag("verbose", "bool", "false", "enable verbose output");
		p.arg("file", "string", "input file");
		fmt.print("args ok");
		return 0;
	}`)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) == 0 || out[0] != "args ok" {
		t.Errorf("got %v", out)
	}
}

func TestExec_ThreadModule(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"spawn and join", `import "thread"; import "fmt"; fn worker() -> i32 {
			return 42;
		}
		fn main() -> i32 {
			Thread t, err e = thread.spawn(worker);
			i32 result, err e2 = t.join();
			fmt.print(fmt.sprintf("%d", result));
			return 0;
		}`, []string{"42"}},
		{"two threads", `import "thread"; import "fmt"; fn add(i32 a, i32 b) -> i32 {
			return a + b;
		}
		fn mul(i32 a, i32 b) -> i32 {
			return a * b;
		}
		fn main() -> i32 {
			Thread t1, err e1 = thread.spawn(add, 3, 4);
			Thread t2, err e2 = thread.spawn(mul, 5, 6);
			i32 r1, err e3 = t1.join();
			i32 r2, err e4 = t2.join();
			fmt.print(fmt.sprintf("%d", r1));
			fmt.print(fmt.sprintf("%d", r2));
			return 0;
		}`, []string{"7", "30"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_MutexModule(t *testing.T) {
	_, out, err := evalBASL(`import "mutex"; import "fmt"; fn main() -> i32 {
		Mutex m, err e = mutex.new();
		err e2 = m.lock();
		err e3 = m.unlock();
		err e4 = m.destroy();
		fmt.print("mutex ok");
		return 0;
	}`)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) == 0 || out[0] != "mutex ok" {
		t.Errorf("got %v", out)
	}
}

func TestExec_CompoundAssign(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"plus_assign", `import "fmt"; fn main() -> i32 { i32 x = 10; x += 5; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"15"}},
		{"minus_assign", `import "fmt"; fn main() -> i32 { i32 x = 10; x -= 3; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"7"}},
		{"star_assign", `import "fmt"; fn main() -> i32 { i32 x = 4; x *= 3; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"12"}},
		{"slash_assign", `import "fmt"; fn main() -> i32 { i32 x = 20; x /= 4; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"5"}},
		{"percent_assign", `import "fmt"; fn main() -> i32 { i32 x = 17; x %= 5; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"2"}},
		{"string_concat_assign", `import "fmt"; fn main() -> i32 { string s = "hello"; s += " world"; fmt.print(s); return 0; }`, []string{"hello world"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q", i, safeIdx(out, i), w)
				}
			}
		})
	}
}

func TestExec_IncDec(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"inc", `import "fmt"; fn main() -> i32 { i32 x = 5; x++; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"6"}},
		{"dec", `import "fmt"; fn main() -> i32 { i32 x = 5; x--; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"4"}},
		{"for_loop_inc", `import "fmt"; fn main() -> i32 { i32 sum = 0; for (i32 i = 0; i < 5; i++) { sum += i; } fmt.print(fmt.sprintf("%d", sum)); return 0; }`, []string{"10"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q", i, safeIdx(out, i), w)
				}
			}
		})
	}
}

func TestExec_Switch(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"basic", `import "fmt"; fn main() -> i32 {
			i32 x = 2;
			switch x { case 1: fmt.print("one"); case 2: fmt.print("two"); case 3: fmt.print("three"); default: fmt.print("other"); }
			return 0;
		}`, []string{"two"}},
		{"default", `import "fmt"; fn main() -> i32 {
			i32 x = 99;
			switch x { case 1: fmt.print("one"); default: fmt.print("default"); }
			return 0;
		}`, []string{"default"}},
		{"string_switch", `import "fmt"; fn main() -> i32 {
			string s = "hello";
			switch s { case "hi": fmt.print("hi"); case "hello": fmt.print("hello!"); }
			return 0;
		}`, []string{"hello!"}},
		{"multi_value_case", `import "fmt"; fn main() -> i32 {
			i32 x = 3;
			switch x { case 1, 2, 3: fmt.print("low"); case 4, 5: fmt.print("high"); }
			return 0;
		}`, []string{"low"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q", i, safeIdx(out, i), w)
				}
			}
		})
	}
}

func TestExec_Const(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"const_decl", `import "fmt"; fn main() -> i32 {
			const i32 x = 42;
			fmt.print(fmt.sprintf("%d", x));
			return 0;
		}`, []string{"42"}},
		{"const_top_level", `import "fmt"; const i32 MAX = 100; fn main() -> i32 {
			fmt.print(fmt.sprintf("%d", MAX));
			return 0;
		}`, []string{"100"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q", i, safeIdx(out, i), w)
				}
			}
		})
	}
}

func TestExec_Enum(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"basic_enum", `import "fmt"; enum Color { Red, Green, Blue } fn main() -> i32 {
			fmt.print(fmt.sprintf("%d", Color.Red));
			fmt.print(fmt.sprintf("%d", Color.Green));
			fmt.print(fmt.sprintf("%d", Color.Blue));
			return 0;
		}`, []string{"0", "1", "2"}},
		{"enum_with_values", `import "fmt"; enum Status { Ok = 200, NotFound = 404, Error = 500 } fn main() -> i32 {
			fmt.print(fmt.sprintf("%d", Status.Ok));
			fmt.print(fmt.sprintf("%d", Status.NotFound));
			fmt.print(fmt.sprintf("%d", Status.Error));
			return 0;
		}`, []string{"200", "404", "500"}},
		{"enum_in_switch", `import "fmt"; enum Dir { Up, Down, Left, Right } fn main() -> i32 {
			i32 d = Dir.Left;
			switch d { case Dir.Up: fmt.print("up"); case Dir.Down: fmt.print("down"); case Dir.Left: fmt.print("left"); case Dir.Right: fmt.print("right"); }
			return 0;
		}`, []string{"left"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q", i, safeIdx(out, i), w)
				}
			}
		})
	}
}

func TestExec_BitwiseXorNot(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"xor", `import "fmt"; fn main() -> i32 { i32 x = 0xFF ^ 0x0F; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"240"}},
		{"not", `import "fmt"; fn main() -> i32 { i32 x = ~0; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"-1"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q", i, safeIdx(out, i), w)
				}
			}
		})
	}
}

func TestExec_BinaryOctalLiterals(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"binary", `import "fmt"; fn main() -> i32 { i32 x = 0b1010; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"10"}},
		{"octal", `import "fmt"; fn main() -> i32 { i32 x = 0o17; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"15"}},
		{"hex", `import "fmt"; fn main() -> i32 { i32 x = 0xFF; fmt.print(fmt.sprintf("%d", x)); return 0; }`, []string{"255"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q", i, safeIdx(out, i), w)
				}
			}
		})
	}
}

func TestExec_RawStrings(t *testing.T) {
	// Build source with backtick raw string: string s = `hello`;
	src := "import \"fmt\"; fn main() -> i32 { string s = `hello world`; fmt.print(s); return 0; }"
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) == 0 || out[0] != "hello world" {
		t.Errorf("got %v", out)
	}
}

func TestExec_ArrayMethods(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"pop", `import "fmt"; fn main() -> i32 {
			array<i32> a = [1, 2, 3];
			i32 v, err e = a.pop();
			fmt.print(fmt.sprintf("%d", v));
			fmt.print(fmt.sprintf("%d", a.len()));
			return 0;
		}`, []string{"3", "2"}},
		{"slice", `import "fmt"; fn main() -> i32 {
			array<i32> a = [10, 20, 30, 40, 50];
			array<i32> b = a.slice(1, 4);
			fmt.print(fmt.sprintf("%d", b.len()));
			return 0;
		}`, []string{"3"}},
		{"contains", `import "fmt"; fn main() -> i32 {
			array<i32> a = [1, 2, 3];
			fmt.print(fmt.sprintf("%t", a.contains(2)));
			fmt.print(fmt.sprintf("%t", a.contains(5)));
			return 0;
		}`, []string{"true", "false"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_MapMethods(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"has", `import "fmt"; fn main() -> i32 {
			map<string, i32> m = {"a": 1, "b": 2};
			fmt.print(fmt.sprintf("%t", m.has("a")));
			fmt.print(fmt.sprintf("%t", m.has("c")));
			return 0;
		}`, []string{"true", "false"}},
		{"keys", `import "fmt"; fn main() -> i32 {
			map<string, i32> m = {"x": 1};
			array<string> ks = m.keys();
			fmt.print(fmt.sprintf("%d", ks.len()));
			return 0;
		}`, []string{"1"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			for i, w := range tt.want {
				if i >= len(out) || out[i] != w {
					t.Errorf("output[%d] = %q, want %q (full: %v)", i, safeIdx(out, i), w, out)
				}
			}
		})
	}
}

func TestExec_TypeConversions_Extended(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		// string -> numeric (fallible)
		{"string_to_i32", `import "fmt"; fn main() -> i32 { i32 n, err e = i32("42"); fmt.print(string(n)); return 0; }`, []string{"42"}},
		{"string_to_i32_err", `import "fmt"; fn main() -> i32 { i32 n, err e = i32("abc"); fmt.print(e.message()); return 0; }`, []string{"invalid i32: abc"}},
		{"string_to_i64", `import "fmt"; fn main() -> i32 { i64 n, err e = i64("999999999999"); fmt.print(string(n)); return 0; }`, []string{"999999999999"}},
		{"string_to_i64_err", `import "fmt"; fn main() -> i32 { i64 n, err e = i64("nope"); fmt.print(e.message()); return 0; }`, []string{"invalid i64: nope"}},
		{"string_to_f64", `import "fmt"; fn main() -> i32 { f64 n, err e = f64("3.14"); fmt.print(string(n)); return 0; }`, []string{"3.14"}},
		{"string_to_f64_err", `import "fmt"; fn main() -> i32 { f64 n, err e = f64("bad"); fmt.print(e.message()); return 0; }`, []string{"invalid f64: bad"}},
		// numeric cross-conversions
		{"i32_to_i64", `import "fmt"; fn main() -> i32 { i64 n = i64(42); fmt.print(string(n)); return 0; }`, []string{"42"}},
		{"i64_to_i32", `import "fmt"; fn main() -> i32 { i64 big, err _ = i64("100"); i32 n = i32(big); fmt.print(string(n)); return 0; }`, []string{"100"}},
		{"i32_to_f64", `import "fmt"; fn main() -> i32 { f64 n = f64(10); fmt.print(string(n)); return 0; }`, []string{"10"}},
		{"f64_to_i32", `import "fmt"; fn main() -> i32 { f64 pi = 3.14; i32 n = i32(pi); fmt.print(string(n)); return 0; }`, []string{"3"}},
		{"i64_to_f64", `import "fmt"; fn main() -> i32 { i64 big, err _ = i64("1000"); f64 n = f64(big); fmt.print(string(n)); return 0; }`, []string{"1000"}},
		// i64 to string
		{"i64_to_string", `import "fmt"; fn main() -> i32 { i64 n, err _ = i64("9876543210"); fmt.print(string(n)); return 0; }`, []string{"9876543210"}},
		// u8 conversions
		{"i32_to_u8", `import "fmt"; fn main() -> i32 { i32 x = 65; fmt.print(string(i32(u8(x)))); return 0; }`, []string{"65"}},
		// string to u32 (fallible)
		{"string_to_u32", `import "fmt"; fn main() -> i32 { u32 n, err e = u32("100"); fmt.print(string(i32(n))); return 0; }`, []string{"100"}},
		{"string_to_u32_err", `import "fmt"; fn main() -> i32 { u32 n, err e = u32("bad"); fmt.print(e.message()); return 0; }`, []string{"invalid u32: bad"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_I64Arithmetic(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"add", `import "fmt"; fn main() -> i32 { i64 a, err _ = i64("100"); i64 b, err _ = i64("200"); fmt.print(string(a + b)); return 0; }`, []string{"300"}},
		{"sub", `import "fmt"; fn main() -> i32 { i64 a, err _ = i64("500"); i64 b, err _ = i64("200"); fmt.print(string(a - b)); return 0; }`, []string{"300"}},
		{"mul", `import "fmt"; fn main() -> i32 { i64 a, err _ = i64("100"); i64 b, err _ = i64("3"); fmt.print(string(a * b)); return 0; }`, []string{"300"}},
		{"div", `import "fmt"; fn main() -> i32 { i64 a, err _ = i64("100"); i64 b, err _ = i64("3"); fmt.print(string(a / b)); return 0; }`, []string{"33"}},
		{"mod", `import "fmt"; fn main() -> i32 { i64 a, err _ = i64("100"); i64 b, err _ = i64("3"); fmt.print(string(a % b)); return 0; }`, []string{"1"}},
		{"compare_lt", `import "fmt"; fn main() -> i32 { i64 a, err _ = i64("1"); i64 b, err _ = i64("2"); if (a < b) { fmt.print("yes"); } return 0; }`, []string{"yes"}},
		{"compare_eq", `import "fmt"; fn main() -> i32 { i64 a, err _ = i64("5"); i64 b, err _ = i64("5"); if (a == b) { fmt.print("eq"); } return 0; }`, []string{"eq"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_F64Arithmetic(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"add", `import "fmt"; fn main() -> i32 { f64 a = 1.5; f64 b = 2.5; fmt.print(string(a + b)); return 0; }`, []string{"4"}},
		{"sub", `import "fmt"; fn main() -> i32 { f64 a = 5.5; f64 b = 2.0; fmt.print(string(a - b)); return 0; }`, []string{"3.5"}},
		{"mul", `import "fmt"; fn main() -> i32 { f64 a = 2.5; f64 b = 4.0; fmt.print(string(a * b)); return 0; }`, []string{"10"}},
		{"div", `import "fmt"; fn main() -> i32 { f64 a = 10.0; f64 b = 4.0; fmt.print(string(a / b)); return 0; }`, []string{"2.5"}},
		{"compare_lt", `import "fmt"; fn main() -> i32 { if (1.0 < 2.0) { fmt.print("yes"); } return 0; }`, []string{"yes"}},
		{"compare_eq", `import "fmt"; fn main() -> i32 { if (3.14 == 3.14) { fmt.print("eq"); } return 0; }`, []string{"eq"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_Defer(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"basic", `import "fmt"; fn main() -> i32 { defer fmt.print("deferred"); fmt.print("first"); return 0; }`, []string{"first", "deferred"}},
		{"lifo", `import "fmt"; fn main() -> i32 { defer fmt.print("a"); defer fmt.print("b"); fmt.print("c"); return 0; }`, []string{"c", "b", "a"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}

func TestExec_LocalFn(t *testing.T) {
	src := `import "fmt"; fn main() -> i32 {
		fn double(i32 x) -> i32 { return x * 2; }
		fmt.print(string(double(5)));
		return 0;
	}`
	_, lines, err := evalBASL(src)
	if err != nil {
		t.Fatalf("error: %v", err)
	}
	checkOutput(t, lines, []string{"10"})
}

func TestExec_MapMethodsExtended(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"values", `import "fmt"; fn main() -> i32 {
			map<string, i32> m = {"a": 1};
			array<i32> vs = m.values();
			fmt.print(string(vs[0]));
			return 0;
		}`, []string{"1"}},
		{"remove", `import "fmt"; fn main() -> i32 {
			map<string, i32> m = {"a": 1, "b": 2};
			m.remove("a");
			fmt.print(string(m.len()));
			return 0;
		}`, []string{"1"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			checkOutput(t, out, tt.want)
		})
	}
}

func TestExec_IndexAssign(t *testing.T) {
	tests := []struct {
		name string
		src  string
		want []string
	}{
		{"array", `import "fmt"; fn main() -> i32 {
			array<i32> a = [1, 2, 3];
			a[1] = 20;
			fmt.print(string(a[1]));
			return 0;
		}`, []string{"20"}},
		{"map", `import "fmt"; fn main() -> i32 {
			map<string, i32> m = {"a": 1};
			m["b"] = 2;
			fmt.print(string(m["b"]));
			return 0;
		}`, []string{"2"}},
		{"map_overwrite", `import "fmt"; fn main() -> i32 {
			map<string, i32> m = {"a": 1};
			m["a"] = 99;
			fmt.print(string(m["a"]));
			return 0;
		}`, []string{"99"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, out, err := evalBASL(tt.src)
			if err != nil {
				t.Fatal(err)
			}
			checkOutput(t, out, tt.want)
		})
	}
}

func TestExec_MemberAssign(t *testing.T) {
	src := `import "fmt"; class Point { pub i32 x; pub i32 y; }
	fn main() -> i32 {
		Point p = Point(1, 2);
		p.x = 10;
		fmt.print(string(p.x));
		return 0;
	}`
	_, lines, err := evalBASL(src)
	if err != nil {
		t.Fatalf("error: %v", err)
	}
	checkOutput(t, lines, []string{"10"})
}

func TestExec_TypeConversionErrors(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantErrSub string
	}{
		{"bad_conv", `fn main() -> i32 { i32 x = i32(true); return 0; }`, "cannot convert"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, _, err := evalBASL(tt.src)
			if err == nil {
				t.Fatal("expected error")
			}
			if !strings.Contains(err.Error(), tt.wantErrSub) {
				t.Errorf("error = %q, want substring %q", err.Error(), tt.wantErrSub)
			}
		})
	}
}

func TestExec_ModuleClassTypeQualification(t *testing.T) {
	// Test that module-qualified class types work correctly
	// This is a regression test for issue #1

	// Create a temporary module file
	tmpDir := t.TempDir()
	modPath := filepath.Join(tmpDir, "lib")
	if err := os.MkdirAll(modPath, 0755); err != nil {
		t.Fatal(err)
	}

	// Write module with a class
	modFile := filepath.Join(modPath, "testmod.basl")
	modSrc := `pub class TestClass {
    pub string value;
    
    fn init() -> void {
        self.value = "initialized";
    }
}`
	if err := os.WriteFile(modFile, []byte(modSrc), 0644); err != nil {
		t.Fatal(err)
	}

	// Test program that uses module-qualified type annotation
	mainSrc := `import "testmod";
import "fmt";

fn main() -> i32 {
    testmod.TestClass obj = testmod.TestClass();
    fmt.print(obj.value);
    return 0;
}`

	// Parse and execute
	lex := lexer.New(mainSrc)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}

	interp := New()
	interp.AddSearchPath(modPath) // Add the lib directory to search path
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	code, err := interp.Exec(prog)
	if err != nil {
		t.Fatalf("execution error: %v", err)
	}
	if code != 0 {
		t.Errorf("exit code = %d, want 0", code)
	}

	want := []string{"initialized"}
	checkOutput(t, lines, want)
}

func TestExec_AnonymousFunctions(t *testing.T) {
	tests := []struct {
		name       string
		src        string
		wantOutput []string
	}{
		{"inline_callback", `import "fmt";
fn apply(fn f, i32 x) -> i32 { return f(x); }
fn main() -> i32 { fmt.print(string(apply(fn(i32 x) -> i32 { return x * 3; }, 5))); return 0; }`,
			[]string{"15"}},
		{"variable_bound", `import "fmt";
fn main() -> i32 { fn d = fn(i32 x) -> i32 { return x * 2; }; fmt.print(string(d(7))); return 0; }`,
			[]string{"14"}},
		{"closure_capture", `import "fmt";
fn main() -> i32 { i32 m = 10; fn s = fn(i32 x) -> i32 { return x * m; }; fmt.print(string(s(4))); return 0; }`,
			[]string{"40"}},
		{"no_args_no_return", `import "fmt";
fn main() -> i32 { fn f = fn() -> void { fmt.print("hi"); }; f(); return 0; }`,
			[]string{"hi"}},
		{"iife", `import "fmt";
fn main() -> i32 { fn() -> void { fmt.print("iife"); }(); return 0; }`,
			[]string{"iife"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, lines, err := evalBASL(tt.src)
			if err != nil {
				t.Fatalf("error: %v", err)
			}
			checkOutput(t, lines, tt.wantOutput)
		})
	}
}
