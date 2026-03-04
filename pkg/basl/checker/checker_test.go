package checker

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestCheckFileValidProgram(t *testing.T) {
	root := t.TempDir()
	libDir := filepath.Join(root, "lib")
	if err := os.MkdirAll(libDir, 0755); err != nil {
		t.Fatalf("os.MkdirAll() error = %v", err)
	}

	writeFile(t, filepath.Join(libDir, "util.basl"), `
pub fn add(i32 a, i32 b) -> i32 {
    return a + b;
}
`)
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "util";

fn main() -> i32 {
    i32 x = util.add(1, 2);
    return x;
}
`)

	diags, err := CheckFile(mainPath, []string{root, libDir})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}
	if len(diags) != 0 {
		t.Fatalf("expected no diagnostics, got %#v", diags)
	}
}

func TestCheckFileReportsArityAndReturnShape(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}

fn pair() -> (i32, err) {
    return (1, ok);
}

fn main() -> i32 {
    i32 x = add(1);
    i32 only = pair();
    i32 a, err b, string c = pair();
    string s = "oops";
    return s;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, "add expects 2 arguments, got 1")
	assertHasDiag(t, diags, "variable only expects a single value, but the expression returns 2 values")
	assertHasDiag(t, diags, "tuple binding expects 3 values, got 2")
	assertHasDiag(t, diags, "return value 1 expects i32, received string")
}

func TestCheckFileReportsInterfaceMismatch(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
interface Greeter {
    fn greet() -> string;
}

class Person implements Greeter {
    fn greet() -> i32 {
        return 1;
    }
}

fn main() -> i32 {
    return 0;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, "class Person method greet return 1 has type i32, interface Greeter requires string")
}

func TestCheckFileAllowsInterfaceAssignmentFromImplementingClass(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
interface Greeter {
    fn greet() -> string;
}

class Person implements Greeter {
    fn greet() -> string {
        return "hi";
    }
}

fn main() -> i32 {
    Greeter g = Person();
    return 0;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}
	if len(diags) != 0 {
		t.Fatalf("expected no diagnostics, got %#v", diags)
	}
}

func TestCheckFileReportsMissingImport(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "missing";

fn main() -> i32 {
    return 0;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, `module "missing" not found`)
}

func TestCheckFileValidatesBuiltinModulesAndMethods(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "path";
import "fmt";

fn main() -> i32 {
    string joined = path.join("a", "b", "c");
    string msg = fmt.sprintf("%s:%d", joined, 1);
    string lower = msg.to_lower();
    i32 n = lower.len();
    return n;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}
	if len(diags) != 0 {
		t.Fatalf("expected no diagnostics, got %#v", diags)
	}
}

func TestCheckFileReportsBuiltinArgTypeMismatch(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "path";

fn main() -> i32 {
    string joined = path.join("a", 1);
    return 0;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, "join arg 2 expects string, received i32")
}

func TestCheckFileReportsMethodArgTypeMismatch(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
fn main() -> i32 {
    string s = "abc";
    array<string> parts = s.split(123);
    return parts.len();
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, "split arg 1 expects string, received i32")
}

func TestCheckFileInfersIndexAndForInTypes(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
fn main() -> i32 {
    array<i32> nums = [1, 2, 3];
    string bad = nums[0];

    for val in nums {
        string other = val;
    }

    return 0;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, "type mismatch in variable bad: expected string, received i32")
	assertHasDiag(t, diags, "type mismatch in variable other: expected string, received i32")
}

func TestCheckFileSupportsArgsAndRegexObjects(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "args";
import "regex";

fn main() -> i32 {
    args.ArgParser p = args.parser("grep", "Search files");
    p.flag("ignore-case", "bool", "false", "Case-insensitive search", "i");
    p.arg("pattern", "string", "Pattern to search for");

    args.Result result, err parseErr = p.parse_result();
    if (parseErr != ok) {
        return 2;
    }

    string pattern = result.get_string("pattern");
    regex.Regex re, err compileErr = regex.compile(pattern);
    if (compileErr != ok) {
        return 2;
    }

    bool matched = re.match("hello");
    return matched ? 0 : 1;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}
	if len(diags) != 0 {
		t.Fatalf("expected no diagnostics, got %#v", diags)
	}
}

func writeFile(t *testing.T, path string, src string) string {
	t.Helper()

	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatalf("os.MkdirAll() error = %v", err)
	}
	data := strings.TrimLeft(src, "\n")
	if err := os.WriteFile(path, []byte(data), 0644); err != nil {
		t.Fatalf("os.WriteFile() error = %v", err)
	}
	return path
}

func assertHasDiag(t *testing.T, diags []Diagnostic, want string) {
	t.Helper()

	for _, diag := range diags {
		if strings.Contains(diag.String(), want) {
			return
		}
	}
	t.Fatalf("missing diagnostic %q in %#v", want, diags)
}
