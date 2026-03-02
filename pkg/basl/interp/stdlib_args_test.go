package interp

import (
	"strings"
	"testing"

	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

func TestArgsParserCreate(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("myapp", "a test app");
	fmt.print("created");
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "created" {
		t.Fatalf("got %v", out)
	}
}

func TestArgsParserFlagDefaults(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	p.flag("verbose", "bool", "false", "enable verbose");
	p.flag("count", "string", "10", "item count");
	map<string, string> result, err e = p.parse();
	fmt.print(result["verbose"]);
	fmt.print(result["count"]);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "false" || out[1] != "10" {
		t.Fatalf("got %v", out)
	}
}

func TestArgsParseResultBasic(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	p.flag("verbose", "bool", "false", "verbose", "v");
	p.flag("count", "i32", "10", "count", "c");
	p.arg("input", "string", "input file");
	
	args.Result result, err e = p.parse_result();
	if (e != ok) {
		fmt.print("error");
		return 1;
	}
	
	bool v = result.get_bool("verbose");
	i32 c = result.get_i32("count");
	string inp = result.get_string("input");
	
	fmt.print(v);
	fmt.print(c);
	fmt.print(inp);
	return 0;
}`
	interp := New()
	interp.scriptArgs = []string{"-v", "-c", "42", "test.txt"}
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}
	_, err = interp.Exec(prog)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) != 3 || lines[0] != "true" || lines[1] != "42" || lines[2] != "test.txt" {
		t.Fatalf("got %v", lines)
	}
}

func TestArgsParseResultVariadic(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	p.arg("files", "string", "files", true);
	
	args.Result result, err e = p.parse_result();
	if (e != ok) {
		fmt.print("error");
		return 1;
	}
	
	array<string> files = result.get_list("files");
	fmt.print(files.len());
	for (i32 i = 0; i < files.len(); i++) {
		fmt.print(files[i]);
	}
	return 0;
}`
	interp := New()
	interp.scriptArgs = []string{"file1.txt", "file2.txt", "file3.txt"}
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}
	_, err = interp.Exec(prog)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) != 4 || lines[0] != "3" || lines[1] != "file1.txt" || lines[2] != "file2.txt" || lines[3] != "file3.txt" {
		t.Fatalf("got %v", lines)
	}
}

func TestArgsParseResultUnknownFlag(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	p.flag("verbose", "bool", "false", "verbose");
	
	args.Result result, err e = p.parse_result();
	if (e != ok) {
		fmt.print("error");
		return 0;
	}
	fmt.print("ok");
	return 0;
}`
	interp := New()
	interp.scriptArgs = []string{"--unknown"}
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}
	_, err = interp.Exec(prog)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) != 1 || lines[0] != "error" {
		t.Fatalf("expected error, got %v", lines)
	}
}

func TestArgsParseResultMissingValue(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	p.flag("output", "string", "out.txt", "output");
	
	args.Result result, err e = p.parse_result();
	if (e != ok) {
		fmt.print("error");
		return 0;
	}
	fmt.print("ok");
	return 0;
}`
	interp := New()
	interp.scriptArgs = []string{"--output"}
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}
	_, err = interp.Exec(prog)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) != 1 || lines[0] != "error" {
		t.Fatalf("expected error, got %v", lines)
	}
}

func TestArgsParseResultEndOfOptions(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	p.arg("files", "string", "files", true);
	
	args.Result result, err e = p.parse_result();
	if (e != ok) {
		fmt.print("error");
		return 1;
	}
	
	array<string> files = result.get_list("files");
	for (i32 i = 0; i < files.len(); i++) {
		fmt.print(files[i]);
	}
	return 0;
}`
	interp := New()
	interp.scriptArgs = []string{"--", "-weird.txt", "--another.txt"}
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}
	_, err = interp.Exec(prog)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) != 2 || lines[0] != "-weird.txt" || lines[1] != "--another.txt" {
		t.Fatalf("got %v", lines)
	}
}

func TestArgsParseResultTypeValidation(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	p.flag("count", "i32", "1", "count");
	
	args.Result result, err e = p.parse_result();
	if (e != ok) {
		fmt.print("error");
		return 0;
	}
	fmt.print("ok");
	return 0;
}`
	interp := New()
	interp.scriptArgs = []string{"--count", "not_a_number"}
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}
	_, err = interp.Exec(prog)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) != 1 || lines[0] != "error" {
		t.Fatalf("expected error for invalid i32, got %v", lines)
	}
}

func TestArgsParseResultInvalidDefault(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	err e = p.flag("count", "i32", "abc", "count");
	if (e != ok) {
		fmt.print("error");
		return 0;
	}
	fmt.print("ok");
	return 0;
}`
	interp := New()
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}
	_, err = interp.Exec(prog)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) != 1 || lines[0] != "error" {
		t.Fatalf("expected error for invalid default, got %v", lines)
	}
}

func TestArgsParseResultNumericGetters(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	args.ArgParser p = args.parser("app", "desc");
	p.flag("count", "i32", "10", "count");
	p.flag("size", "i64", "1000", "size");
	p.flag("ratio", "f64", "3.14", "ratio");
	
	args.Result result, err e = p.parse_result();
	if (e != ok) {
		fmt.print("error");
		return 1;
	}
	
	i32 c = result.get_i32("count");
	i64 s = result.get_i64("size");
	f64 r = result.get_f64("ratio");
	
	fmt.print(c);
	fmt.print(s);
	fmt.print(r);
	return 0;
}`
	interp := New()
	interp.scriptArgs = []string{"--count", "42", "--size", "9999", "--ratio", "2.71"}
	var lines []string
	interp.PrintFn = func(s string) { lines = append(lines, strings.TrimRight(s, "\n")) }

	lex := lexer.New(src)
	tokens, err := lex.Tokenize()
	if err != nil {
		t.Fatal(err)
	}
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		t.Fatal(err)
	}
	_, err = interp.Exec(prog)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) != 3 || lines[0] != "42" || lines[1] != "9999" || lines[2] != "2.71" {
		t.Fatalf("got %v", lines)
	}
}
