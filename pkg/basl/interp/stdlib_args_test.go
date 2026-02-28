package interp

import "testing"

func TestArgsParserCreate(t *testing.T) {
	src := `import "fmt"; import "args";
fn main() -> i32 {
	ArgParser p = args.parser("myapp", "a test app");
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
	ArgParser p = args.parser("app", "desc");
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
