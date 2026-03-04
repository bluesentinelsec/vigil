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
    string empty = path.join();
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

func TestCheckFileSeparatesParsingFromConversions(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "parse";

fn main() -> i32 {
    i32 parsed, err parseErr = parse.i32("42");
    bool enabled, err boolErr = parse.bool("true");
    i32 bad = i32("42");
    return enabled ? parsed : bad;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, "cannot convert string to i32; use parse.i32(...) for string parsing")
	if len(diags) != 1 {
		t.Fatalf("expected exactly 1 diagnostic, got %#v", diags)
	}
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

func TestCheckFileUsesExactOptionalBuiltinArity(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "args";
import "fmt";
import "os";
import "path";

fn main() -> i32 {
    args.ArgParser p = args.parser("grep", "Search files");
    err tooManyFlag = p.flag("ignore-case", "bool", "false", "Case-insensitive search", "i", "extra");
    err tooManyArg = p.arg("pattern", "string", "Pattern to search for", false, true);

    string formatted = fmt.sprintf("%s:%s", "a", "b");
    string joined = path.join("a", "b", "c");
    string stdout, string stderr, i32 code, err execErr = os.exec("echo", "hello", "world");

    return 0;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, "flag expects 4 to 5 arguments, got 6")
	assertHasDiag(t, diags, "arg expects 3 to 4 arguments, got 5")
	if len(diags) != 2 {
		t.Fatalf("expected exactly 2 diagnostics, got %#v", diags)
	}
}

func TestCheckFileSupportsExtendedStdlibCoverage(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "archive";
import "args";
import "base64";
import "compress";
import "crypto";
import "csv";
import "ffi";
import "file";
import "hash";
import "hex";
import "http";
import "json";
import "log";
import "math";
import "mime";
import "mutex";
import "sort";
import "sqlite";
import "strings";
import "tcp";
import "test";
import "thread";
import "time";
import "udp";
import "unsafe";
import "xml";

fn worker(i32 x) -> i32 {
    return x + 1;
}

fn cmp(string a, string b) -> bool {
    return a < b;
}

fn on_log(string level, string msg) -> void {
    return;
}

fn handle(HttpRequest req) -> i32 {
    return 200;
}

fn main() -> i32 {
    f64 rootNum = math.sqrt(9.0);
    f64 circle = math.pi;
    string combined = strings.join(["a", "b"], ",");

    i64 nowMs = time.now();
    i64 parsedMs, err parsedErr = time.parse("2006-01-02", "2024-01-15");

    json.Value data, err jsonErr = json.parse("{\"name\":\"alice\",\"items\":[\"x\"]}");
    string name = data.get_string("name");
    array<string> keys = data.keys();

    xml.Value doc, err xmlErr = xml.parse("<root><item id=\"1\">x</item></root>");
    array<xml.Value> items = doc.find("item");
    string tag = doc.tag();

    HttpResponse resp, err httpErr = http.get("https://example.com");
    i32 status = resp.status;
    http.listen(":8080", handle);

    TcpConn conn, err tcpErr = tcp.connect("127.0.0.1:80");
    UdpConn udpConn, err udpErr = udp.listen("127.0.0.1:9000");

    SqliteDB db, err dbErr = sqlite.open(":memory:");
    err execErr = db.exec("CREATE TABLE t (id INTEGER)");
    SqliteRows rows, err queryErr = db.query("SELECT id FROM t");

    Thread th, err threadErr = thread.spawn(worker, 1);
    i32 joined, err joinErr = th.join();
    Mutex mu, err mutexErr = mutex.new();
    err lockErr = mu.lock();
    err unlockErr = mu.unlock();
    err destroyErr = mu.destroy();

    test.T tt = test.T();
    tt.assert(true, "ok");
    tt.fail("still type-checks");

    array<string> parts = ["b", "a"];
    sort.strings(parts);
    sort.by(parts, cmp);

    log.set_handler(on_log);
    log.set_level("debug");
    log.info("hello");

    string enc64 = base64.encode("hi");
    string dec64, err dec64Err = base64.decode(enc64);
    string encHex = hex.encode("hi");
    string decHex, err decHexErr = hex.decode(encHex);
    string digest = hash.sha256("hello");
    string contentType = mime.type_by_ext(".txt");

    array<array<string>> parsedRows, err csvErr = csv.parse("a,b\n1,2\n");
    string csvText, err csvTextErr = csv.stringify(parsedRows);

    err tarErr = archive.tar_create("out.tar", ["a.txt"]);
    string gz, err gzErr = compress.gzip("data");
    string cipher, err cipherErr = crypto.aes_encrypt("00112233445566778899aabbccddeeff", "hello");

    ffi.Lib lib, err libErr = ffi.load("./libexample.so");
    ffi.Func add, err bindErr = ffi.bind(lib, "add", "i32", "i32", "i32");
    i32 ffiResult = add(2, 3);
    err closeErr = lib.close();

    unsafe.Buffer buf = unsafe.alloc(4);
    i32 bufLen = buf.len();
    buf.set(0, 65);
    u8 b = buf.get(0);
    unsafe.Layout layout = unsafe.layout("i32", "i32");
    unsafe.Struct st = layout.new();
    st.set(0, 42);
    i32 first = st.get(0);
    unsafe.Callback cb = unsafe.callback(worker, "i32", "i32");
    unsafe.Ptr p = cb.ptr();
    cb.free();

    bool exists = file.exists("main.basl");
    args.ArgParser ap = args.parser("tool", "desc");
    err flagErr = ap.flag("verbose", "bool", "false", "Verbose");

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

func TestCheckFileReportsExtendedSemanticErrors(t *testing.T) {
	root := t.TempDir()
	mainPath := writeFile(t, filepath.Join(root, "main.basl"), `
import "math";
import "sort";
import "thread";

fn worker(i32 x) -> i32 {
    return x + 1;
}

fn bad_cmp(string a) -> string {
    return a;
}

fn missing(i32 x) -> i32 {
    if (x > 0) {
        return x;
    }
}

fn main() -> i32 {
    for val in "oops" {
        i32 copy = val;
    }

    array<i32> nums = [1, 2, 3];
    i32 badIndex = nums["0"];

    switch (1) {
    case "1":
        break;
    default:
        break;
    }

    i32 badSpawn, err spawnErr = thread.spawn(worker, "x");
    sort.by(["a", "b"], bad_cmp);
    f64 nope = math.nope(1.0);

    return 0;
}
`)

	diags, err := CheckFile(mainPath, []string{root})
	if err != nil {
		t.Fatalf("CheckFile() error = %v", err)
	}

	assertHasDiag(t, diags, "function missing may exit without returning 1 values")
	assertHasDiag(t, diags, "for-in expects array or map, received string")
	assertHasDiag(t, diags, "array index must be i32, received string")
	assertHasDiag(t, diags, "switch case expects i32, received string")
	assertHasDiag(t, diags, "thread.spawn arg 1 expects i32, received string")
	assertHasDiag(t, diags, "sort.by comparator expects 2 arguments, got 1")
	assertHasDiag(t, diags, "module member \"nope\" not found")
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
