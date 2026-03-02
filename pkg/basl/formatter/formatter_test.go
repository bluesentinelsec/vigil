package formatter

import (
	"strings"
	"testing"

	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

func fmtSource(src string) string {
	lex := lexer.New(src)
	tokensWithComments, _ := lex.TokenizeWithComments()
	lex2 := lexer.New(src)
	tokens, _ := lex2.Tokenize()
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		return "PARSE ERROR: " + err.Error()
	}
	return string(Format(prog, tokensWithComments))
}

func TestFormatImports(t *testing.T) {
	src := `import "io";
import "fmt";
`
	got := fmtSource(src)
	want := `import "fmt";
import "io";
`
	if got != want {
		t.Errorf("imports:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatFunction(t *testing.T) {
	src := `fn main() -> i32 {
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    return 0;
}
`
	if got != want {
		t.Errorf("function:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatExpandSingleLineIf(t *testing.T) {
	// Single-line if must be expanded to multi-line
	src := `fn main() -> i32 {
if (x > 0) { return x; }
return 0;
}
`
	got := fmtSource(src)
	if strings.Contains(got, "{ return x; }") {
		t.Errorf("single-line if was NOT expanded:\n%s", got)
	}
	want := `fn main() -> i32 {
    if (x > 0) {
        return x;
    }
    return 0;
}
`
	if got != want {
		t.Errorf("expand if:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatClass(t *testing.T) {
	src := `class Pet {
pub string name;
pub i32 age;
fn init(string name, i32 age) -> void {
self.name = name;
self.age = age;
}
fn get_name() -> string { return self.name; }
}
`
	got := fmtSource(src)
	want := `class Pet {
    pub string name;
    pub i32 age;

    fn init(string name, i32 age) -> void {
        self.name = name;
        self.age = age;
    }

    fn get_name() -> string {
        return self.name;
    }
}
`
	if got != want {
		t.Errorf("class:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatComment(t *testing.T) {
	src := `// top comment
fn main() -> i32 {
// body comment
return 0;
}
`
	got := fmtSource(src)
	want := `// top comment
fn main() -> i32 {
    // body comment
    return 0;
}
`
	if got != want {
		t.Errorf("comment:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatForLoop(t *testing.T) {
	src := `fn main() -> i32 {
for (i32 i = 0; i < 10; i++) {
fmt.println(i);
}
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    for (i32 i = 0; i < 10; i++) {
        fmt.println(i);
    }
    return 0;
}
`
	if got != want {
		t.Errorf("for loop:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatSwitch(t *testing.T) {
	src := `fn main() -> i32 {
switch (x) {
case 1: return 1;
default: return 0;
}
}
`
	got := fmtSource(src)
	// switch cases should be indented, and single-line case bodies expanded
	if !strings.Contains(got, "    switch") {
		t.Errorf("switch not indented:\n%s", got)
	}
	if !strings.Contains(got, "        case 1:") {
		t.Errorf("case not indented:\n%s", got)
	}
}

func TestFormatIdempotent(t *testing.T) {
	src := `import "fmt";

fn main() -> i32 {
    fmt.println("hello");
    return 0;
}
`
	first := fmtSource(src)
	second := fmtSource(first)
	if first != second {
		t.Errorf("not idempotent:\nfirst:\n%s\nsecond:\n%s", first, second)
	}
}

func TestFormatIfElse(t *testing.T) {
	src := `fn main() -> i32 {
if (x > 0) {
return 1;
} else {
return 0;
}
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    if (x > 0) {
        return 1;
    } else {
        return 0;
    }
}
`
	if got != want {
		t.Errorf("if-else:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatWhile(t *testing.T) {
	src := `fn main() -> i32 {
while (x > 0) {
x = x - 1;
}
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    while (x > 0) {
        x = x - 1;
    }
    return 0;
}
`
	if got != want {
		t.Errorf("while:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatEnum(t *testing.T) {
	src := `enum Color {
Red,
Green = 5,
Blue
}
`
	got := fmtSource(src)
	want := `enum Color {
    Red,
    Green = 5,
    Blue
}
`
	if got != want {
		t.Errorf("enum:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatInterface(t *testing.T) {
	src := `interface Drawable {
fn draw() -> void;
fn area() -> f64;
}
`
	got := fmtSource(src)
	want := `interface Drawable {
    fn draw() -> void;
    fn area() -> f64;
}
`
	if got != want {
		t.Errorf("interface:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatTupleBind(t *testing.T) {
	src := `fn main() -> i32 {
string name, err e = io.read_string("Enter: ");
if (e != ok) { return 1; }
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    string name, err e = io.read_string("Enter: ");
    if (e != ok) {
        return 1;
    }
    return 0;
}
`
	if got != want {
		t.Errorf("tuple bind:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatForIn(t *testing.T) {
	src := `fn main() -> i32 {
for item in items {
fmt.println(item);
}
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    for item in items {
        fmt.println(item);
    }
    return 0;
}
`
	if got != want {
		t.Errorf("for-in:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatDefer(t *testing.T) {
	src := `fn main() -> i32 {
defer cleanup();
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    defer cleanup();
    return 0;
}
`
	if got != want {
		t.Errorf("defer:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatCompoundAssign(t *testing.T) {
	src := `fn main() -> i32 {
i32 x = 0;
x += 1;
x -= 2;
x++;
return x;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    i32 x = 0;
    x += 1;
    x -= 2;
    x++;
    return x;
}
`
	if got != want {
		t.Errorf("compound assign:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatBlockComment(t *testing.T) {
	src := `/* block comment */
fn main() -> i32 {
return 0;
}
`
	got := fmtSource(src)
	want := `/* block comment */
fn main() -> i32 {
    return 0;
}
`
	if got != want {
		t.Errorf("block comment:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatConstDecl(t *testing.T) {
	src := `const i32 MAX = 100;
fn main() -> i32 {
return MAX;
}
`
	got := fmtSource(src)
	want := `const i32 MAX = 100;

fn main() -> i32 {
    return MAX;
}
`
	if got != want {
		t.Errorf("const decl:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatArrayMapTypes(t *testing.T) {
	src := `fn main() -> i32 {
array<i32> nums = [1, 2, 3];
map<string, i32> m = {"a": 1, "b": 2};
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    array<i32> nums = [1, 2, 3];
    map<string, i32> m = {"a": 1, "b": 2};
    return 0;
}
`
	if got != want {
		t.Errorf("array/map types:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatTernaryOperator(t *testing.T) {
	src := `fn main() -> i32 {
i32 x=true?1:2;
string s=x>3?"big":"small";
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    i32 x = true ? 1 : 2;
    string s = x > 3 ? "big" : "small";
    return 0;
}
`
	if got != want {
		t.Errorf("ternary:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatChainedTernary(t *testing.T) {
	src := `fn main() -> i32 {
string size=x<10?"small":x<100?"medium":"large";
return 0;
}
`
	got := fmtSource(src)
	want := `fn main() -> i32 {
    string size = x < 10 ? "small" : x < 100 ? "medium" : "large";
    return 0;
}
`
	if got != want {
		t.Errorf("chained ternary:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatTernaryRoundTrip(t *testing.T) {
	// Test that formatting is idempotent (round-trip)
	src := `fn main() -> i32 {
    i32 max = a > b ? a : b;
    string status = age >= 18 ? "adult" : "minor";
    string size = x < 10 ? "small" : x < 100 ? "medium" : "large";
    return 0;
}
`
	got := fmtSource(src)
	if got != src {
		t.Errorf("ternary round-trip failed:\ngot:\n%s\nwant:\n%s", got, src)
	}

	// Format again to ensure idempotency
	got2 := fmtSource(got)
	if got2 != got {
		t.Errorf("ternary not idempotent:\nfirst:\n%s\nsecond:\n%s", got, got2)
	}
}
