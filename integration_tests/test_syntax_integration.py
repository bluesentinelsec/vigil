#!/usr/bin/env python3
"""Integration tests for BASL syntax semantics via the CLI interpreter."""

from __future__ import annotations

import http.server
import platform
import socket
import subprocess
import tempfile
import threading
import textwrap
import unittest
from pathlib import Path

try:
    import pexpect
except ModuleNotFoundError:
    pexpect = None

IS_WINDOWS = platform.system() == "Windows"


REPO_ROOT = Path(__file__).resolve().parents[1]
BASL_BIN = REPO_ROOT / "basl"


class BaslSyntaxIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not BASL_BIN.exists():
            raise FileNotFoundError(f"missing interpreter binary: {BASL_BIN}")

    def _run_basl(
        self,
        source: str,
        *,
        files: dict[str, str] | None = None,
        main_rel: str = "main.basl",
        script_args: list[str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        files = files or {}
        script_args = script_args or []
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            root = Path(td)

            for rel_path, contents in files.items():
                file_path = root / rel_path
                file_path.parent.mkdir(parents=True, exist_ok=True)
                file_path.write_text(textwrap.dedent(contents).strip() + "\n", encoding="utf-8")

            main_path = root / main_rel
            main_path.parent.mkdir(parents=True, exist_ok=True)
            main_path.write_text(textwrap.dedent(source).strip() + "\n", encoding="utf-8")

            return subprocess.run(
                [str(BASL_BIN), str(main_path), *script_args],
                cwd=root,
                capture_output=True,
                text=True,
            )

    def _assert_success(
        self,
        source: str,
        *,
        stdout: str = "",
        code: int = 0,
        files: dict[str, str] | None = None,
        main_rel: str = "main.basl",
        script_args: list[str] | None = None,
    ) -> None:
        proc = self._run_basl(source, files=files, main_rel=main_rel, script_args=script_args)
        self.assertEqual(
            proc.returncode,
            code,
            msg=f"exit code mismatch\nstdout: {proc.stdout!r}\nstderr: {proc.stderr!r}",
        )
        self.assertEqual(proc.stderr, "", msg=f"unexpected stderr: {proc.stderr!r}")
        self.assertEqual(proc.stdout, stdout, msg=f"stdout mismatch: {proc.stdout!r}")

    def _assert_failure(
        self,
        source: str,
        err_sub: str,
        *,
        files: dict[str, str] | None = None,
        main_rel: str = "main.basl",
        script_args: list[str] | None = None,
    ) -> None:
        proc = self._run_basl(source, files=files, main_rel=main_rel, script_args=script_args)
        self.assertNotEqual(
            proc.returncode,
            0,
            msg=f"expected failure\nstdout: {proc.stdout!r}\nstderr: {proc.stderr!r}",
        )
        self.assertIn(
            err_sub,
            proc.stderr,
            msg=f"stderr missing {err_sub!r}\nstdout: {proc.stdout!r}\nstderr: {proc.stderr!r}",
        )

    def test_program_structure_exit_code(self) -> None:
        self._assert_success("fn main() -> i32 { return 42; }", code=42)

    def test_imports_builtin_alias_relative_and_pub(self) -> None:
        files = {
            "shared/utils.basl": """
                pub fn greet() -> string { return "hi"; }
                pub i32 N = 7;
                fn hidden() -> string { return "hidden"; }
            """
        }
        source = """
            import "fmt" as f;
            import "../shared/utils";

            fn main() -> i32 {
                f.print(utils.greet() + ":" + string(utils.N));
                return 0;
            }
        """
        self._assert_success(source, stdout="hi:7", files=files, main_rel="app/main.basl")

    def test_imports_non_pub_is_hidden(self) -> None:
        files = {
            "shared/utils.basl": """
                pub fn shown() -> string { return "ok"; }
                fn hidden() -> string { return "no"; }
            """
        }
        source = """
            import "../shared/utils";
            fn main() -> i32 {
                utils.hidden();
                return 0;
            }
        """
        self._assert_failure(
            source,
            "module has no member",
            files=files,
            main_rel="app/main.basl",
        )

    def test_import_cycle_reports_chain(self) -> None:
        files = {
            "a.basl": """
                import "b";
                pub fn a_value() -> i32 { return 1; }
            """,
            "b.basl": """
                import "a";
                pub fn b_value() -> i32 { return 2; }
            """,
        }
        source = """
            import "a";

            fn main() -> i32 {
                return 0;
            }
        """
        self._assert_failure(source, "import cycle detected: a -> b -> a", files=files)

    def test_types_literals_and_explicit_conversions(self) -> None:
        source = """
            import "fmt";
            import "parse";

            fn main() -> i32 {
                i32 dec = 255;
                i32 hex = 0xFF;
                i32 bin = 0b11111111;
                i32 oct = 0o377;

                i32 a = 42;
                i64 b = i64(a);
                f64 c = f64(a);
                u8 d = u8(7);
                u32 e = u32(9);
                u64 f = u64(11);
                bool g = true;
                string h = "ok";
                err er = ok;

                i32 parsed, err pe = parse.i32("42");
                array<array<string>> grid = [["a", "b"], ["c"]];
                string nested, err ne = grid[0].get(1);

                fmt.print(string(dec) + "," + string(hex) + "," + string(bin) + "," + string(oct));
                fmt.print("|" + string(a) + "," + string(b) + "," + string(c) + "," + string(d) + "," + string(e) + "," + string(f) + "," + string(g) + "," + h + "," + pe.message());
                fmt.print("|" + string(parsed) + ":" + er.message() + ":" + nested + ":" + ne.message() + ":" + string(grid.len()));
                return 0;
            }
        """
        self._assert_success(stdout="255,255,255,255|42,42,42,7,9,11,true,ok,|42::b::2", source=source)

    def test_string_literals_and_interpolation(self) -> None:
        source = """
            import "fmt";

            fn main() -> i32 {
                string esc = "x\\ny";
                string raw = `x\\ny`;
                string name = "Alice";
                f64 pi = 3.14159;
                fmt.print(string(esc.len()));
                fmt.print("|" + raw);
                fmt.print("|" + f"{name.to_upper()}:{pi:.2f}:{{set}}");
                return 0;
            }
        """
        self._assert_success(source, stdout="3|x\\ny|ALICE:3.14:{set}")

    def test_variables_constants_assignments_and_operators(self) -> None:
        source = """
            import "fmt";
            const i32 TOP = 5;

            class Box {
                i32 v;
                fn init(i32 v) -> void { self.v = v; }
            }

            fn side() -> bool {
                fmt.print("SIDE");
                return true;
            }

            fn main() -> i32 {
                i32 x = 10;
                x += 5;
                x -= 3;
                x *= 2;
                x /= 4;
                x %= 5;
                x++;
                x--;

                array<i32> a = [1, 2];
                a[0] = 9;

                map<string, i32> m = {"k": 1};
                m["k"] = 7;
                err me = m.set("z", 3);

                Box b = Box(1);
                b.v = 4;

                bool sc = false && side();
                bool sc2 = true || side();

                i32 bit = (6 & 3) | (8 >> 2) ^ (1 << 3);
                i32 notv = ~0;
                bool cmp = (3 < 4) && (4 <= 4) && (5 > 4) && (5 >= 5) && (5 == 5) && (5 != 6) && !false;

                string s = "hi" + " " + "there";
                s += "!";

                fmt.print(string(TOP) + ":" + string(x) + ":" + string(a[0]) + ":" + string(m["k"]) + ":" + string(m["z"]) + ":" + string(b.v) + ":" + string(sc) + ":" + string(sc2) + ":" + string(bit) + ":" + string(notv) + ":" + string(cmp) + ":" + s + ":" + me.message());
                return 0;
            }
        """
        self._assert_success(source, stdout="5:1:9:7:3:4:false:true:10:-1:true:hi there!:")

    def test_functions_multi_return_fn_values_and_local_functions(self) -> None:
        source = """
            import "fmt";

            fn add(i32 a, i32 b) -> i32 { return a + b; }

            fn divide(i32 a, i32 b) -> (i32, err) {
                if (b == 0) { return (0, err("div0", err.arg)); }
                return (a / b, ok);
            }

            fn trio() -> (i32, string, bool) {
                return (7, "skip", true);
            }

            fn apply(fn(i32, i32) -> i32 op, i32 a, i32 b) -> i32 {
                return op(a, b);
            }

            fn main() -> i32 {
                fn any = add;
                fn(i32, i32) -> i32 typed = add;
                i32 q, err _ = divide(9, 2);
                i32 zero, err e = divide(1, 0);
                i32 tv, string _, bool tb = trio();

                fn helper(i32 x) -> i32 {
                    return x * 2;
                }

                fmt.print(string(apply(typed, 2, 3)) + ":" + string(any(4, 5)) + ":" + string(q) + ":" + e.message() + ":" + string(helper(6)) + ":" + string(zero) + ":" + string(tv) + ":" + string(tb));
                return 0;
            }
        """
        self._assert_success(source, stdout="5:9:4:div0:12:0:7:true")

    def test_anonymous_functions_and_closures(self) -> None:
        source = """
            import "fmt";

            fn apply(fn(i32) -> i32 op, i32 x) -> i32 {
                return op(x);
            }

            fn main() -> i32 {
                i32 factor = 10;
                fn scale = fn(i32 x) -> i32 { return x * factor; };

                fmt.print(string(apply(fn(i32 x) -> i32 { return x + 1; }, 14)));
                fmt.print(":" + string(scale(4)) + ":");

                fn(fn(i32) -> i32 cb) -> void {
                    fmt.print(string(cb(3)));
                }(fn(i32 x) -> i32 { return x * 2; });

                return 0;
            }
        """
        self._assert_success(source, stdout="15:40:6")

    def test_nested_generics_err_literal_and_non_err_discards(self) -> None:
        source = """
            import "fmt";

            fn many() -> (i32, string, bool) {
                return (7, "kept", false);
            }

            fn main() -> i32 {
                array<array<string>> grid = [["a"], ["b", "c"]];
                grid[1].push("d");

                err boom = err("boom", err.io);
                i32 _, string kept, bool _ = many();

                fmt.print(string(grid.len()) + ":" + string(grid[1].len()) + ":" + grid[1][1]);
                fmt.print("|" + boom.message() + ":" + string(boom != ok));
                fmt.print("|" + kept);
                return 0;
            }
        """
        self._assert_success(source, stdout="2:3:c|boom:true|kept")

    def test_control_flow_if_while_for_forin_switch_break_continue(self) -> None:
        source = """
            import "fmt";

            fn main() -> i32 {
                i32 x = 7;
                if (x > 10) {
                    fmt.print("big");
                } else if (x > 5) {
                    fmt.print("mid");
                } else {
                    fmt.print("small");
                }

                i32 sum = 0;
                for (i32 i = 0; i < 5; i++) {
                    if (i == 2) { continue; }
                    sum += i;
                    if (i == 4) { break; }
                }

                i32 w = 0;
                while (w < 3) { w++; }

                array<i32> arr = [1, 2, 3];
                i32 acc = 0;
                for v in arr { acc += v; }

                map<string, i32> m = {"a": 1, "b": 2};
                i32 kv = 0;
                string kvs = "";
                for k, v in m {
                    if (k == "a") { kv += v; }
                    if (k == "b") { kv += v; }
                    kvs += k + string(v);
                }

                i32 only = 0;
                string onlys = "";
                for v in m {
                    only += v;
                    onlys += string(v);
                }

                i32 sw = 2;
                switch (sw) {
                    case 1: fmt.print(":bad");
                    case 2, 3: fmt.print(":ok");
                    default: fmt.print(":bad2");
                }
                switch (1) {
                    case 1: fmt.print(":one");
                    case 1: fmt.print(":fall");
                    default: fmt.print(":def");
                }

                fmt.print(":" + string(sum) + ":" + string(w) + ":" + string(acc) + ":" + string(kv) + ":" + string(only) + ":" + kvs + ":" + onlys);
                return 0;
            }
        """
        self._assert_success(source, stdout="mid:ok:one:8:3:6:3:3:a1b2:12")

    def test_pub_fields_and_composition_across_module_boundary(self) -> None:
        files = {
            "models.basl": """
                pub class Point {
                    pub i32 x;
                    pub i32 y;

                    fn init(i32 x, i32 y) -> void {
                        self.x = x;
                        self.y = y;
                    }
                }

                pub class Segment {
                    pub Point a;
                    pub Point b;

                    fn init(Point a, Point b) -> void {
                        self.a = a;
                        self.b = b;
                    }

                    fn dx() -> i32 {
                        return self.b.x - self.a.x;
                    }
                }
            """
        }
        source = """
            import "fmt";
            import "models";

            fn main() -> i32 {
                models.Point p = models.Point(2, 3);
                models.Segment s = models.Segment(models.Point(2, 3), models.Point(9, 6));
                fmt.print(string(p.x) + ":" + string(s.a.y) + ":" + string(s.dx()));
                return 0;
            }
        """
        self._assert_success(source, stdout="2:3:7", files=files)

    def test_stdlib_smoke_remaining_modules(self) -> None:
        source = """
            import "fmt";
            import "regex";

            fn main() -> i32 {
                bool matched, err me = regex.match("^[a-z]+$", "basl");
                string first, err fe = regex.find("[0-9]+", "a1b22");
                array<string> found, err ae = regex.find_all("[0-9]+", "a1b22c333d");
                string replaced, err re = regex.replace("[0-9]+", "a1b22", "#");
                array<string> parts, err se = regex.split("[0-9]+", "a1b22c333d");
                string bad, err be = regex.find("(", "x");

                fmt.print(fmt.sprintf("%s:%d", "hits", found.len()));
                fmt.print("|" + fmt.dollar(12));
                fmt.println("|" + replaced);
                fmt.print(
                    first + ":" +
                    string(matched) + ":" +
                    found[2] + ":" +
                    string(parts.len()) + ":" +
                    parts[2] + ":" +
                    string(fe == ok) +
                    string(ae == ok) +
                    string(re == ok) +
                    string(se == ok) + ":" +
                    bad + ":" +
                    string(be != ok)
                );
                return 0;
            }
        """
        self._assert_success(
            source,
            stdout="hits:3|$12.00|a#b#\n1:true:333:4:c:truetruetruetrue::true",
        )

    def test_stdlib_gui_module_surface(self) -> None:
        source = """
            import "fmt";
            import "gui";

            fn main() -> i32 {
                bool supported = gui.supported();
                string backend = gui.backend();

                gui.AppOpts appOpts = gui.app_opts();
                gui.WindowOpts windowOpts = gui.window_opts("Demo");
                windowOpts.width = 640;
                windowOpts.height = 360;
                gui.BoxOpts boxOpts = gui.box_opts();
                boxOpts.vertical = true;
                boxOpts.spacing = 12;
                boxOpts.padding = 20;
                gui.GridOpts gridOpts = gui.grid_opts();
                gridOpts.row_spacing = 14;
                gridOpts.col_spacing = 18;
                gui.CellOpts cellOpts = gui.cell_opts(2, 3);
                cellOpts.row_span = 2;
                cellOpts.col_span = 4;
                gui.LabelOpts labelOpts = gui.label_opts("Hello");
                gui.ButtonOpts buttonOpts = gui.button_opts("Save");
                buttonOpts.width = 120;
                buttonOpts.height = 32;
                buttonOpts.on_click = fn() -> void { fmt.print("cb"); };
                gui.EntryOpts entryOpts = gui.entry_opts();
                entryOpts.text = "seed";
                entryOpts.width = 260;

                fmt.print(
                    string(backend.len() > 0) + ":" +
                    string(supported || !supported) + ":" +
                    string(windowOpts.width) + ":" +
                    string(boxOpts.padding) + ":" +
                    string(gridOpts.row_spacing) + ":" +
                    string(cellOpts.col_span) + ":" +
                    labelOpts.text + ":" +
                    buttonOpts.text + ":" +
                    entryOpts.text
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="true:true:640:20:14:4:Hello:Save:seed")

    def test_stdlib_args_module(self) -> None:
        source = """
            import "fmt";
            import "args";

            fn main() -> i32 {
                args.ArgParser p = args.parser("demo", "args regression");
                err e1 = p.flag("verbose", "bool", "false", "enable verbose mode");
                err e2 = p.flag("count", "i32", "10", "item count");
                err e3 = p.flag("output", "string", "out.txt", "output path");
                err e4 = p.arg("input", "string", "input file");
                err e5 = p.arg("mode", "string", "mode");

                map<string, string> result, err pe = p.parse();

                fmt.print(
                    result["verbose"] + ":" +
                    result["count"] + ":" +
                    result["output"] + ":" +
                    result["input"] + ":" +
                    result["mode"] + ":" +
                    e1.message() + e2.message() + e3.message() + e4.message() + e5.message() + pe.message()
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="false:10:out.txt:::")
        self._assert_success(
            source,
            stdout="true:7:log.txt:input.basl:fast:",
            script_args=["input.basl", "--verbose", "--count", "7", "--output", "log.txt", "fast"],
        )

    def test_stdlib_hex_module(self) -> None:
        source = """
            import "fmt";
            import "hex";

            fn main() -> i32 {
                string encoded = hex.encode("AB");
                string decoded, err de = hex.decode(encoded);
                string bad, err be = hex.decode("abc");

                fmt.print(
                    encoded + ":" +
                    decoded + ":" +
                    string(de == ok) + ":" +
                    bad + ":" +
                    string(be != ok) + ":" +
                    string(be.message().len() > 0)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="4142:AB:true::true:true")

    def test_stdlib_hash_module(self) -> None:
        source = """
            import "fmt";
            import "hash";

            fn main() -> i32 {
                string md5v = hash.md5("hello");
                string sha1v = hash.sha1("hello");
                string sha256v = hash.sha256("hello");
                string sha512v = hash.sha512("hello");
                string hmacv = hash.hmac_sha256("secret", "message");

                fmt.print(md5v);
                fmt.print("|" + sha1v);
                fmt.print("|" + sha256v);
                fmt.print("|" + string(sha512v.len()));
                fmt.print("|" + string(hmacv.len()));
                return 0;
            }
        """
        self._assert_success(
            source,
            stdout="5d41402abc4b2a76b9719d911017c592|aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d|2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824|128|64",
        )

    def test_stdlib_csv_module(self) -> None:
        source = """
            import "fmt";
            import "csv";

            fn main() -> i32 {
                array<array<string>> rows, err pe = csv.parse("name,age\\nalice,30\\n");
                string out = csv.stringify(rows);
                array<array<string>> bad_rows, err be = csv.parse("a,b\\n1\\n");

                fmt.print(
                    string(rows.len()) + ":" +
                    rows[0][0] + ":" +
                    rows[1][1] + ":" +
                    string(pe == ok) + ":" +
                    string(out == "name,age\\nalice,30\\n") + ":" +
                    string(bad_rows.len()) + ":" +
                    string(be != ok) + ":" +
                    string(be.message().len() > 0)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="2:name:30:true:true:0:true:true")

    def test_stdlib_mime_module(self) -> None:
        source = """
            import "fmt";
            import "mime";

            fn main() -> i32 {
                string json_t = mime.type_by_ext(".json");
                string html_t = mime.type_by_ext("html");
                string unknown_t = mime.type_by_ext(".definitely_not_real");
                bool html_ok = html_t == "text/html; charset=utf-8" || html_t == "text/html";

                fmt.print(
                    json_t + ":" +
                    string(html_ok) + ":" +
                    string(unknown_t == "")
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="application/json:true:true")

    def test_stdlib_xml_module(self) -> None:
        source = """
            import "fmt";
            import "xml";

            fn main() -> i32 {
                xml.Value root, err pe = xml.parse("<book id=\\"42\\"><title>Hello</title><item>A</item><item>B</item></book>");
                xml.Value title, err fe = root.find_one("title");
                string id, bool found = root.attr("id");
                string missing, bool missing_found = root.attr("missing");
                array<xml.Value> items = root.find("item");
                xml.Value none, err me = root.find_one("missing");
                xml.Value bad, err be = xml.parse("");

                fmt.print(
                    root.tag() + ":" +
                    title.text() + ":" +
                    id + ":" +
                    string(found) + ":" +
                    missing + ":" +
                    string(missing_found) + ":" +
                    string(root.len()) + ":" +
                    string(items.len()) + ":" +
                    string(pe == ok) + ":" +
                    string(fe == ok) + ":" +
                    string(me != ok) + ":" +
                    string(be != ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="book:Hello:42:true::false:3:2:true:true:true:true")

    def test_stdlib_rand_module(self) -> None:
        source = """
            import "fmt";
            import "rand";
            import "regex";

            fn main() -> i32 {
                string bytes16 = rand.bytes(16);
                string bytes4 = rand.bytes(4);
                i32 n = rand.int(10, 20);
                bool hexish, err re = regex.match("^[0-9a-f]+$", bytes4);

                fmt.print(
                    string(bytes16.len()) + ":" +
                    string(bytes4.len()) + ":" +
                    string(n >= 10) + ":" +
                    string(n < 20) + ":" +
                    string(hexish) + ":" +
                    string(re == ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="32:8:true:true:true:true")

    def test_stdlib_archive_module(self) -> None:
        source = """
            import "fmt";
            import "file";
            import "archive";

            fn main() -> i32 {
                err fw1 = file.write_all("alpha.txt", "A");
                err fw2 = file.write_all("beta.txt", "B");

                err t1 = archive.tar_create("bundle.tar", ["alpha.txt", "beta.txt"]);
                err t2 = archive.tar_extract("bundle.tar", "tar_out");
                string ta, err tr1 = file.read_all("tar_out/alpha.txt");
                string tb, err tr2 = file.read_all("tar_out/beta.txt");

                err z1 = archive.zip_create("bundle.zip", ["alpha.txt", "beta.txt"]);
                err z2 = archive.zip_extract("bundle.zip", "zip_out");
                string za, err zr1 = file.read_all("zip_out/alpha.txt");
                string zb, err zr2 = file.read_all("zip_out/beta.txt");

                fmt.print(
                    string(fw1 == ok) + ":" +
                    string(fw2 == ok) + ":" +
                    string(t1 == ok) + ":" +
                    string(t2 == ok) + ":" +
                    ta + tb + ":" +
                    string(tr1 == ok) + ":" +
                    string(tr2 == ok) + ":" +
                    string(z1 == ok) + ":" +
                    string(z2 == ok) + ":" +
                    za + zb + ":" +
                    string(zr1 == ok) + ":" +
                    string(zr2 == ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="true:true:true:true:AB:true:true:true:true:AB:true:true")

    def test_stdlib_compress_module(self) -> None:
        source = """
            import "fmt";
            import "compress";

            fn main() -> i32 {
                string gz, err ge = compress.gzip("hello world");
                string gunzipped, err gd = compress.gunzip(gz);
                string bad_gz, err bge = compress.gunzip("not gzip");

                string zl, err ze = compress.zlib("test data");
                string unzl, err zd = compress.unzlib(zl);
                string bad_zl, err bze = compress.unzlib("not zlib");

                fmt.print(
                    string(ge == ok) + ":" +
                    gunzipped + ":" +
                    string(gd == ok) + ":" +
                    bad_gz + ":" +
                    string(bge != ok) + ":" +
                    string(ze == ok) + ":" +
                    unzl + ":" +
                    string(zd == ok) + ":" +
                    bad_zl + ":" +
                    string(bze != ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="true:hello world:true::true:true:test data:true::true")

    def test_stdlib_log_module(self) -> None:
        source = """
            import "fmt";
            import "log";

            fn handler(string level, string msg) -> void {
                fmt.print(level + ":" + msg + "|");
            }

            fn main() -> i32 {
                log.set_level("warn");
                log.set_handler(handler);
                log.info("skip");
                log.warn("careful");
                log.error("bad");
                return 0;
            }
        """
        self._assert_success(source, stdout="WARN:careful|ERROR:bad|")
        self._assert_failure(
            """
                import "log";
                fn main() -> i32 {
                    log.set_level("bogus");
                    return 0;
                }
            """,
            'log.set_level: unknown level "bogus"',
        )

    def test_stdlib_math_module(self) -> None:
        source = """
            import "fmt";
            import "math";

            fn main() -> i32 {
                f64 r = math.random();
                fmt.print(
                    string(math.sqrt(4.0)) + ":" +
                    string(math.pow(2.0, 10.0)) + ":" +
                    string(math.floor(2.7)) + ":" +
                    string(math.ceil(2.3)) + ":" +
                    string(math.round(2.5)) + ":" +
                    string(math.min(3.0, 7.0)) + ":" +
                    string(math.max(3.0, 7.0)) + ":" +
                    string(math.pi > 3.14) + ":" +
                    string(math.e > 2.71) + ":" +
                    string(r >= 0.0) + ":" +
                    string(r < 1.0)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="2:1024:2:3:3:3:7:true:true:true:true")
        self._assert_failure(
            """
                import "math";
                fn main() -> i32 {
                    math.sqrt(4);
                    return 0;
                }
            """,
            "math.sqrt: expected f64 argument",
        )

    def test_stdlib_json_module(self) -> None:
        source = """
            import "fmt";
            import "json";

            fn main() -> i32 {
                json.Value obj, err pe = json.parse("{\\\"name\\\":\\\"alice\\\",\\\"age\\\":30,\\\"active\\\":true,\\\"nums\\\":[1,2]}");
                json.Value nums, err ge = obj.get("nums");
                json.Value miss, err me = obj.get("missing");
                json.Value atv, err ae = nums.at(1);
                json.Value oob, err oe = nums.at(5);
                array<string> keys = obj.keys();
                string out, err se = json.stringify({"ok": 1});
                json.Value bad, err be = json.parse("not json");

                fmt.print(
                    obj.get_string("name") + ":" +
                    string(obj.get_i32("age")) + ":" +
                    string(obj.get_bool("active")) + ":" +
                    obj.get_string("missing") + ":" +
                    string(nums.len()) + ":" +
                    string(nums.at_i32(0)) + ":" +
                    nums.at_string(1) + ":" +
                    string(keys.len()) + ":" +
                    string(pe == ok) + ":" +
                    string(ge == ok) + ":" +
                    string(me != ok) + ":" +
                    string(ae == ok) + ":" +
                    string(oe != ok) + ":" +
                    out + ":" +
                    string(se == ok) + ":" +
                    string(be != ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="alice:30:true::2:1:2:4:true:true:true:true:true:{\"ok\":1}:true:true")

    def test_stdlib_path_module(self) -> None:
        source = """
            import "fmt";
            import "path";

            fn main() -> i32 {
                string joined = path.join("a", "b", "c.txt");
                string abs_path, err ae = path.abs("demo.txt");

                fmt.print(
                    string(path.base(joined) == "c.txt") + ":" +
                    string(path.ext(joined) == ".txt") + ":" +
                    string(path.dir(joined) == path.join("a", "b")) + ":" +
                    string(path.base(path.dir(joined)) == "b") + ":" +
                    string(path.ext("noext") == "") + ":" +
                    string(path.base(abs_path) == "demo.txt") + ":" +
                    string(ae == ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="true:true:true:true:true:true:true")

    def test_stdlib_file_module(self) -> None:
        source = """
            import "fmt";
            import "file";

            fn main() -> i32 {
                err mk = file.mkdir("data");
                err w1 = file.write_all("data/note.txt", "a\\nb\\n");
                err ap = file.append("data/note.txt", "c");
                string all, err ra = file.read_all("data/note.txt");
                array<string> lines, err rl = file.read_lines("data/note.txt");
                array<string> entries, err ld = file.list_dir("data");
                bool exists_before = file.exists("data/note.txt");
                err rn = file.rename("data/note.txt", "data/note2.txt");
                bool exists_after_old = file.exists("data/note.txt");
                bool exists_after_new = file.exists("data/note2.txt");
                file.FileStat st, err se = file.stat("data/note2.txt");
                file.File fh, err fo = file.open("data/note2.txt", "r");
                string l1, err r1 = fh.read_line();
                string l2, err r2 = fh.read_line();
                string l3, err r3 = fh.read_line();
                string l4, err r4 = fh.read_line();
                err fc = fh.close();
                file.File bad, err be = file.open("data/note2.txt", "bad");
                err rm = file.remove("data/note2.txt");
                bool exists_final = file.exists("data/note2.txt");

                fmt.print(
                    string(mk == ok) + ":" +
                    string(w1 == ok) + ":" +
                    string(ap == ok) + ":" +
                    all + ":" +
                    string(ra == ok) + ":" +
                    string(lines.len()) + ":" +
                    lines[1] + ":" +
                    string(rl == ok) + ":" +
                    string(entries.len()) + ":" +
                    entries[0] + ":" +
                    string(ld == ok) + ":" +
                    string(exists_before) + ":" +
                    string(rn == ok) + ":" +
                    string(exists_after_old) + ":" +
                    string(exists_after_new) + ":" +
                    st.name + ":" +
                    string(st.size) + ":" +
                    string(st.is_dir) + ":" +
                    string(se == ok) + ":" +
                    string(fo == ok) + ":" +
                    l1 + l2 + l3 + ":" +
                    string(r1 == ok) + ":" +
                    string(r2 == ok) + ":" +
                    string(r3 == ok) + ":" +
                    l4 + ":" +
                    string(r4 != ok) + ":" +
                    string(fc == ok) + ":" +
                    string(be != ok) + ":" +
                    string(rm == ok) + ":" +
                    string(exists_final)
                );
                return 0;
            }
        """
        self._assert_success(
            source,
            stdout="true:true:true:a\nb\nc:true:3:b:true:1:note.txt:true:true:true:false:true:note2.txt:5:false:true:true:abc:true:true:true::true:true:true:true:false",
        )

    def test_stdlib_file_walk_modes(self) -> None:
        source = """
            import "fmt";
            import "file";

            fn main() -> i32 {
                err mk = file.mkdir("data/sub");
                err w1 = file.write_all("data/one.txt", "1");
                err w2 = file.write_all("data/sub/two.txt", "2");

                array<file.Entry> strict, err se = file.walk("data");
                array<file.Entry> strictFollow, err sfe = file.walk_follow_links("data");
                array<file.Entry> best, array<file.WalkIssue> issues = file.walk_best_effort("missing");
                array<file.Entry> bestFollow, array<file.WalkIssue> issuesFollow = file.walk_follow_links_best_effort("missing");

                fmt.print(
                    string(mk == ok) + ":" +
                    string(w1 == ok) + ":" +
                    string(w2 == ok) + ":" +
                    string(se == ok) + ":" +
                    string(strict.len()) + ":" +
                    string(sfe == ok) + ":" +
                    string(strictFollow.len()) + ":" +
                    string(best.len()) + ":" +
                    string(issues.len()) + ":" +
                    issues[0].err.kind() + ":" +
                    string(bestFollow.len()) + ":" +
                    string(issuesFollow.len()) + ":" +
                    issuesFollow[0].err.kind()
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="true:true:true:true:4:true:4:0:1:not_found:0:1:not_found")

    def test_stdlib_base64_module(self) -> None:
        source = """
            import "fmt";
            import "base64";

            fn main() -> i32 {
                string enc = base64.encode("hello world");
                string dec, err de = base64.decode(enc);
                string bad, err be = base64.decode("!!!invalid!!!");

                fmt.print(
                    enc + ":" +
                    dec + ":" +
                    string(de == ok) + ":" +
                    bad + ":" +
                    string(be != ok) + ":" +
                    string(be.message().len() > 0)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="aGVsbG8gd29ybGQ=:hello world:true::true:true")

    def test_stdlib_strings_module(self) -> None:
        source = """
            import "fmt";
            import "strings";

            fn main() -> i32 {
                fmt.print(
                    strings.join(["x", "y", "z"], ",") + ":" +
                    strings.join(["a", "b"], "") + ":" +
                    strings.repeat("ab", 3)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="x,y,z:ab:ababab")
        self._assert_failure(
            """
                import "strings";
                fn main() -> i32 {
                    strings.repeat("ab", "3");
                    return 0;
                }
            """,
            "strings.repeat: expected (string, i32)",
        )

    def test_stdlib_sort_module(self) -> None:
        source = """
            import "fmt";
            import "sort";

            fn desc(i32 a, i32 b) -> bool {
                return a > b;
            }

            fn main() -> i32 {
                array<i32> ints = [3, 1, 2];
                array<string> strs = ["c", "a", "b"];
                array<i32> custom = [4, 1, 3];

                sort.ints(ints);
                sort.strings(strs);
                sort.by(custom, desc);

                fmt.print(
                    string(ints[0]) + string(ints[1]) + string(ints[2]) + ":" +
                    strs[0] + strs[1] + strs[2] + ":" +
                    string(custom[0]) + string(custom[1]) + string(custom[2])
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="123:abc:431")

    def test_stdlib_os_module(self) -> None:
        # Use a simple command that works on all platforms
        if IS_WINDOWS:
            test_cmd = 'cmd.exe'
            test_args = ['/c', 'echo', 'test']
        else:
            test_cmd = 'echo'
            test_args = ['test']
        
        test_cmd_escaped = test_cmd.replace("\\", "\\\\")
        test_args_str = ', '.join(f'"{arg}"' for arg in test_args)
        
        source = f"""
            import "fmt";
            import "os";

            fn main() -> i32 {{
                err se = os.set_env("BASL_IT_ENV", "hello");
                string env_v, bool env_ok = os.env("BASL_IT_ENV");
                string miss_v, bool miss_ok = os.env("BASL_IT_MISSING");
                string cwd, err ce = os.cwd();
                string host, err he = os.hostname();
                string out, string errs, i32 exitCode, err xe = os.exec("{test_cmd_escaped}", {test_args_str});

                fmt.print(
                    string(se == ok) + ":" +
                    env_v + ":" +
                    string(env_ok) + ":" +
                    miss_v + ":" +
                    string(miss_ok) + ":" +
                    string(os.platform().len() > 0) + ":" +
                    string(cwd.len() > 0) + ":" +
                    string(ce == ok) + ":" +
                    string(host.len() > 0) + ":" +
                    string(he == ok) + ":" +
                    string(out.len() > 0) + ":" +
                    string(xe == ok)
                );
                return 0;
            }}
        """
        # All platforms should now work
        expected = "true:hello:true::false:true:true:true:true:true:true:true"
        self._assert_success(source, stdout=expected)

    def test_stdlib_time_module(self) -> None:
        source = """
            import "fmt";
            import "time";

            fn main() -> i32 {
                i64 start = time.now();
                time.sleep(10);
                i64 end = time.now();
                i64 elapsed = time.since(start);
                i64 parsed, err pe = time.parse("2006-01-02", "2000-01-01");
                string fixed = time.format(parsed, "2006");
                i64 bad, err be = time.parse("2006-01-02", "nope");

                fmt.print(
                    string(start > i64(0)) + ":" +
                    string(end >= start) + ":" +
                    string(elapsed >= i64(0)) + ":" +
                    string(fixed.len() == 4) + ":" +
                    string(parsed > i64(0)) + ":" +
                    string(pe == ok) + ":" +
                    string(bad == i64(0)) + ":" +
                    string(be != ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="true:true:true:true:true:true:true:true")
        self._assert_failure(
            """
                import "time";
                fn main() -> i32 {
                    time.sleep("10");
                    return 0;
                }
            """,
            "time.sleep: expected i32 milliseconds",
        )

    def test_stdlib_crypto_module(self) -> None:
        source = """
            import "fmt";
            import "crypto";
            import "rand";

            fn main() -> i32 {
                string key = rand.bytes(32);
                string ct, err ee = crypto.aes_encrypt(key, "secret message");
                string pt, err de = crypto.aes_decrypt(key, ct);
                string bad_ct, err be = crypto.aes_encrypt("badkey", "data");
                string wrong, err we = crypto.aes_decrypt(rand.bytes(32), ct);

                fmt.print(
                    string(ct.len() > 0) + ":" +
                    pt + ":" +
                    string(ee == ok) + ":" +
                    string(de == ok) + ":" +
                    bad_ct + ":" +
                    string(be != ok) + ":" +
                    wrong + ":" +
                    string(we != ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="true:secret message:true:true::true::true")

    def test_stdlib_sqlite_module(self) -> None:
        source = """
            import "fmt";
            import "sqlite";

            fn main() -> i32 {
                SqliteDB db, err oe = sqlite.open(":memory:");
                err ce = db.exec("CREATE TABLE t (name TEXT, age INTEGER)");
                err i1 = db.exec("INSERT INTO t VALUES (?, ?)", "ann", 11);
                err i2 = db.exec("INSERT INTO t VALUES (?, ?)", "bob", 7);
                SqliteRows rows, err qe = db.query("SELECT name, age FROM t WHERE age >= ? ORDER BY age", 7);

                bool first = rows.next();
                string n1 = rows.get("name");
                string a1 = rows.get("age");

                bool second = rows.next();
                string n2 = rows.get("name");
                string a2 = rows.get("age");

                bool third = rows.next();
                err rc = rows.close();
                err dc = db.close();

                fmt.print(
                    string(oe == ok) + ":" +
                    string(ce == ok) + ":" +
                    string(i1 == ok) + ":" +
                    string(i2 == ok) + ":" +
                    string(qe == ok) + ":" +
                    string(first) + ":" +
                    n1 + ":" + a1 + ":" +
                    string(second) + ":" +
                    n2 + ":" + a2 + ":" +
                    string(third) + ":" +
                    string(rc == ok) + ":" +
                    string(dc == ok)
                );
                return 0;
            }
        """
        self._assert_success(source, stdout="true:true:true:true:true:true:bob:7:true:ann:11:false:true:true")

    def test_stdlib_http_module(self) -> None:
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self) -> None:  # noqa: N802
                if self.path == "/hello":
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(b"world")
                    return
                self.send_response(404)
                self.end_headers()

            def do_POST(self) -> None:  # noqa: N802
                body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                self.send_response(200)
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, format: str, *args: object) -> None:
                return

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        port = server.server_address[1]

        try:
            source = f"""
                import "fmt";
                import "http";

                fn main() -> i32 {{
                    HttpResponse g, err ge = http.get("http://127.0.0.1:{port}/hello");
                    HttpResponse p, err pe = http.post("http://127.0.0.1:{port}/echo", "payload");

                    fmt.print(
                        string(ge == ok) + ":" +
                        string(g.status) + ":" +
                        g.body + ":" +
                        string(pe == ok) + ":" +
                        p.body
                    );
                    return 0;
                }}
            """
            self._assert_success(source, stdout="true:200:world:true:payload")
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)

    def test_stdlib_tcp_module(self) -> None:
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]
        done = threading.Event()

        def serve() -> None:
            try:
                conn, _ = listener.accept()
                with conn:
                    data = conn.recv(1024)
                    conn.sendall(data)
            except OSError:
                pass
            finally:
                done.set()
                listener.close()

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()

        source = f"""
            import "fmt";
            import "tcp";

            fn main() -> i32 {{
                TcpConn conn, err ce = tcp.connect("127.0.0.1:{port}");
                err we = conn.write("ping");
                string data, err re = conn.read(1024);
                err xe = conn.close();

                fmt.print(
                    string(ce == ok) + ":" +
                    string(we == ok) + ":" +
                    data + ":" +
                    string(re == ok) + ":" +
                    string(xe == ok)
                );
                return 0;
            }}
        """
        try:
            self._assert_success(source, stdout="true:true:ping:true:true")
            self.assertTrue(done.wait(timeout=5), msg="tcp server did not complete")
        finally:
            listener.close()
            thread.join(timeout=2)

    def test_stdlib_udp_module(self) -> None:
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        server.settimeout(5)
        port = server.getsockname()[1]
        done = threading.Event()
        received: dict[str, str] = {}

        def receive() -> None:
            try:
                data, _ = server.recvfrom(1024)
                received["msg"] = data.decode("utf-8")
            except OSError:
                pass
            finally:
                done.set()
                server.close()

        thread = threading.Thread(target=receive, daemon=True)
        thread.start()

        source = f"""
            import "fmt";
            import "udp";

            fn main() -> i32 {{
                err se = udp.send("127.0.0.1:{port}", "hello udp");
                fmt.print(string(se == ok));
                return 0;
            }}
        """
        try:
            self._assert_success(source, stdout="true")
            self.assertTrue(done.wait(timeout=5), msg="udp receiver did not receive a datagram")
            self.assertEqual(received.get("msg"), "hello udp")
        finally:
            server.close()
            thread.join(timeout=2)

    def test_stdlib_io_module(self) -> None:
        if pexpect is None:
            self.skipTest("pexpect is not installed")
        if IS_WINDOWS:
            self.skipTest("pexpect.spawn not available on Windows")

        with tempfile.TemporaryDirectory(prefix="basl_io_") as td:
            root = Path(td)
            main_path = root / "main.basl"
            main_path.write_text(
                textwrap.dedent(
                    """
                    import "fmt";
                    import "io";

                    fn main() -> i32 {
                        string line, err e1 = io.read_line();
                        string name, err e2 = io.input("Name: ");
                        i32 age, err e3 = io.read_i32("Age: ");
                        f64 price, err e4 = io.read_f64("Price: ");
                        string note, err e5 = io.read_string("Note: ");
                        i32 bad, err e6 = io.read_i32("BadInt: ");

                        fmt.print(line + ":" + string(e1 == ok));
                        fmt.print("|" + name + ":" + string(e2 == ok));
                        fmt.print("|" + string(age) + ":" + string(e3 == ok));
                        fmt.print("|" + string(price) + ":" + string(e4 == ok));
                        fmt.print("|" + note + ":" + string(e5 == ok));
                        fmt.print("|" + string(bad) + ":" + string(e6 != ok) + ":" + e6.message());
                        return 0;
                    }
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )

            child = pexpect.spawn(
                str(BASL_BIN),
                [str(main_path)],
                cwd=str(root),
                encoding="utf-8",
                timeout=5,
                echo=False,
            )

            try:
                child.sendline("first line")
                child.expect_exact("Name: ")
                child.sendline("Alice")
                child.expect_exact("Age: ")
                child.sendline("42")
                child.expect_exact("Price: ")
                child.sendline("3.5")
                child.expect_exact("Note: ")
                child.sendline("memo")
                child.expect_exact("BadInt: ")
                child.sendline("oops")
                child.expect(pexpect.EOF)

                self.assertEqual(
                    child.before,
                    "first line:true|Alice:true|42:true|3.5:true|memo:true|0:true:invalid integer",
                    msg=f"io stdout mismatch: {child.before!r}",
                )
            finally:
                child.close()

            self.assertEqual(child.exitstatus, 0, msg=f"unexpected io exit status: {child.exitstatus!r}")

    def test_stdlib_ffi_module(self) -> None:
        source = """
            import "fmt";
            import "ffi";

            fn main() -> i32 {
                ffi.Lib _, err le = ffi.load("./definitely_missing_library_xyz");
                fmt.print(string(le != ok) + ":" + string(le.message().len() > 0));
                return 0;
            }
        """
        self._assert_success(source, stdout="true:true")
        self._assert_failure(
            """
                import "ffi";

                fn main() -> i32 {
                    ffi.bind("not-a-lib", "puts", "i32");
                    return 0;
                }
            """,
            "ffi.bind: first arg must be ffi.Lib",
        )

    def test_package_builds_standalone_binary(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_pkg_") as td:
            root = Path(td)

            (root / "app").mkdir(parents=True, exist_ok=True)
            (root / "shared").mkdir(parents=True, exist_ok=True)

            (root / "shared" / "util.basl").write_text(
                textwrap.dedent(
                    """
                    pub fn greet() -> string {
                        return "hi";
                    }
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            (root / "app" / "main.basl").write_text(
                textwrap.dedent(
                    """
                    import "fmt";
                    import "os";
                    import "../shared/util";

                    fn main() -> i32 {
                        array<string> argv = os.args();
                        fmt.print(util.greet() + ":" + argv[0]);
                        return 0;
                    }
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )

            out_bin = root / "packaged_app"
            build_proc = subprocess.run(
                [str(BASL_BIN), "package", "-o", str(out_bin), str(root / "app" / "main.basl")],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                build_proc.returncode,
                0,
                msg=f"package build failed\nstdout: {build_proc.stdout!r}\nstderr: {build_proc.stderr!r}",
            )
            self.assertEqual(build_proc.stdout, "", msg=f"unexpected package stdout: {build_proc.stdout!r}")
            self.assertEqual(build_proc.stderr, "", msg=f"unexpected package stderr: {build_proc.stderr!r}")
            self.assertTrue(out_bin.exists(), msg=f"missing packaged binary: {out_bin}")

            inspect_proc = subprocess.run(
                [str(BASL_BIN), "package", "--inspect", str(out_bin)],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                inspect_proc.returncode,
                0,
                msg=f"package inspect failed\nstdout: {inspect_proc.stdout!r}\nstderr: {inspect_proc.stderr!r}",
            )
            self.assertEqual(inspect_proc.stderr, "", msg=f"unexpected inspect stderr: {inspect_proc.stderr!r}")
            self.assertIn("ENTRY\n  entry.basl", inspect_proc.stdout)
            self.assertIn("FILES", inspect_proc.stdout)
            self.assertIn("  entry.basl", inspect_proc.stdout)
            self.assertIn("  pkg/mod001.basl", inspect_proc.stdout)

            run_proc = subprocess.run(
                [str(out_bin), "ARG"],
                cwd=root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                run_proc.returncode,
                0,
                msg=f"packaged app failed\nstdout: {run_proc.stdout!r}\nstderr: {run_proc.stderr!r}",
            )
            self.assertEqual(run_proc.stderr, "", msg=f"unexpected packaged stderr: {run_proc.stderr!r}")
            self.assertEqual(run_proc.stdout, "hi:ARG", msg=f"packaged stdout mismatch: {run_proc.stdout!r}")

    def test_project_layout_auto_resolves_lib_for_run_test_and_package(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_project_") as td:
            root = Path(td)

            (root / "lib").mkdir(parents=True, exist_ok=True)
            (root / "test").mkdir(parents=True, exist_ok=True)

            (root / "basl.toml").write_text('name = "demo"\nversion = "0.1.0"\n', encoding="utf-8")
            (root / "main.basl").write_text(
                textwrap.dedent(
                    """
                    import "fmt";
                    import "util";

                    fn main() -> i32 {
                        fmt.print(util.answer());
                        return 0;
                    }
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            (root / "lib" / "util.basl").write_text(
                textwrap.dedent(
                    """
                    pub fn answer() -> string {
                        return "project";
                    }
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            (root / "test" / "util_test.basl").write_text(
                textwrap.dedent(
                    """
                    import "test";
                    import "util";

                    fn test_answer(test.T t) -> void {
                        t.assert(util.answer() == "project", "util.answer should resolve from lib/");
                    }
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )

            run_proc = subprocess.run(
                [str(BASL_BIN), str(root / "main.basl")],
                cwd=root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                run_proc.returncode,
                0,
                msg=f"project run failed\nstdout: {run_proc.stdout!r}\nstderr: {run_proc.stderr!r}",
            )
            self.assertEqual(run_proc.stderr, "", msg=f"unexpected project run stderr: {run_proc.stderr!r}")
            self.assertEqual(run_proc.stdout, "project", msg=f"project run stdout mismatch: {run_proc.stdout!r}")

            test_proc = subprocess.run(
                [str(BASL_BIN), "test"],
                cwd=root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                test_proc.returncode,
                0,
                msg=f"project test failed\nstdout: {test_proc.stdout!r}\nstderr: {test_proc.stderr!r}",
            )
            self.assertEqual(test_proc.stderr, "", msg=f"unexpected project test stderr: {test_proc.stderr!r}")
            self.assertIn("PASS: 1 passed", test_proc.stdout)

            pkg_proc = subprocess.run(
                [str(BASL_BIN), "package"],
                cwd=root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                pkg_proc.returncode,
                0,
                msg=f"project package failed\nstdout: {pkg_proc.stdout!r}\nstderr: {pkg_proc.stderr!r}",
            )
            self.assertEqual(pkg_proc.stdout, "", msg=f"unexpected project package stdout: {pkg_proc.stdout!r}")
            self.assertEqual(pkg_proc.stderr, "", msg=f"unexpected project package stderr: {pkg_proc.stderr!r}")

            out_bin = root / root.name
            self.assertTrue(out_bin.exists(), msg=f"missing project packaged binary: {out_bin}")

            packaged_run = subprocess.run(
                [str(out_bin)],
                cwd=root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                packaged_run.returncode,
                0,
                msg=f"project packaged run failed\nstdout: {packaged_run.stdout!r}\nstderr: {packaged_run.stderr!r}",
            )
            self.assertEqual(packaged_run.stderr, "", msg=f"unexpected project packaged stderr: {packaged_run.stderr!r}")
            self.assertEqual(packaged_run.stdout, "project", msg=f"project packaged stdout mismatch: {packaged_run.stdout!r}")

    def test_editor_list_install_and_uninstall(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_editor_home_") as td:
            home = Path(td)

            list_proc = subprocess.run(
                [str(BASL_BIN), "editor", "list"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                list_proc.returncode,
                0,
                msg=f"editor list failed\nstdout: {list_proc.stdout!r}\nstderr: {list_proc.stderr!r}",
            )
            self.assertEqual(list_proc.stderr, "", msg=f"unexpected editor list stderr: {list_proc.stderr!r}")
            self.assertIn("vim", list_proc.stdout)
            self.assertIn("nvim", list_proc.stdout)
            self.assertIn("vscode", list_proc.stdout)

            install_proc = subprocess.run(
                [str(BASL_BIN), "editor", "install", "--home", str(home), "vim", "vscode"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                install_proc.returncode,
                0,
                msg=f"editor install failed\nstdout: {install_proc.stdout!r}\nstderr: {install_proc.stderr!r}",
            )
            self.assertEqual(install_proc.stderr, "", msg=f"unexpected editor install stderr: {install_proc.stderr!r}")
            self.assertTrue((home / ".vim" / "syntax" / "basl.vim").exists(), msg="vim syntax file was not installed")
            self.assertTrue(
                (home / ".vscode" / "extensions" / "basl" / "package.json").exists(),
                msg="vscode extension package.json was not installed",
            )

            uninstall_proc = subprocess.run(
                [str(BASL_BIN), "editor", "uninstall", "--home", str(home), "vim", "vscode"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                uninstall_proc.returncode,
                0,
                msg=f"editor uninstall failed\nstdout: {uninstall_proc.stdout!r}\nstderr: {uninstall_proc.stderr!r}",
            )
            self.assertEqual(uninstall_proc.stderr, "", msg=f"unexpected editor uninstall stderr: {uninstall_proc.stderr!r}")
            self.assertFalse((home / ".vim" / "syntax" / "basl.vim").exists(), msg="vim syntax file still exists")
            self.assertFalse((home / ".vscode" / "extensions" / "basl").exists(), msg="vscode extension dir still exists")

    def test_enums_interfaces_classes_and_fallible_construction(self) -> None:
        source = """
            import "fmt";

            enum Color { Red, Green, Blue }
            enum HttpStatus { Ok = 200, NotFound = 404, Error = 500 }

            interface Drawable {
                fn draw() -> void;
                fn get_name() -> string;
            }

            interface Sizer {
                fn size() -> i32;
            }

            class Circle implements Drawable, Sizer {
                string label;
                fn init(string label) -> void { self.label = label; }
                fn draw() -> void { fmt.print("D"); }
                fn get_name() -> string { return self.label; }
                fn size() -> i32 { return 1; }
            }

            fn render(Drawable d) -> void {
                d.draw();
                fmt.print(d.get_name());
            }

            class Connection {
                string host;
                fn init(string host) -> err {
                    if (host == "") { return err("missing host", err.arg); }
                    self.host = host;
                    return ok;
                }
            }

            fn main() -> i32 {
                i32 c = Color.Green;
                i32 s = HttpStatus.NotFound;
                switch (c) {
                    case Color.Red: fmt.print("R");
                    case Color.Green: fmt.print("G");
                    default: fmt.print("X");
                }

                Circle x = Circle("c1");
                array<Drawable> objs = [x];
                for o in objs { render(o); }

                Connection good, err ge = Connection("localhost");
                Connection bad, err be = Connection("");
                fmt.print(":" + string(s) + ":" + good.host + ":" + ge.message() + ":" + be.message());
                return 0;
            }
        """
        self._assert_success(source, stdout="GDc1:404:localhost::missing host")

    def test_error_values_defer_lifo_eager_and_conversions(self) -> None:
        source = """
            import "fmt";
            import "file";
            import "parse";

            fn emit(i32 x) -> void {
                fmt.print(string(x));
            }

            fn main() -> i32 {
                i32 a = 1;
                defer emit(a);
                a = 2;
                defer emit(a);

                i32 n, err e1 = parse.i32("42");
                i32 bad, err e2 = parse.i32("x");
                string _, err fe = file.read_all("definitely_missing_file.basl");
                err manual = err("boom", err.io);

                if (e1 == ok) { fmt.print(":ok"); }
                if (e2 != ok) { fmt.print(":" + e2.message()); }
                if (fe != ok) { fmt.print(":file"); }
                if (manual != ok) { fmt.print(":manual=" + manual.message()); }
                fmt.print(":" + string(n) + ":" + string(bad));
                return 0;
            }
        """
        self._assert_success(source, stdout=":ok:invalid i32: x:file:manual=boom:42:021")

    def test_guard_statement(self) -> None:
        source = """
            import "fmt";

            fn load(bool should_succeed) -> (string, err) {
                if (should_succeed) {
                    return ("ready", ok);
                }
                return ("", err("missing", err.not_found));
            }

            fn main() -> i32 {
                guard string good, err okErr = load(true) {
                    fmt.print("bad");
                    return 1;
                }
                fmt.print(good + ":" + okErr.message());

                guard string missing, err missErr = load(false) {
                    fmt.print(":" + missErr.message());
                }
                fmt.print(":" + missing);
                return 0;
            }
        """
        self._assert_success(source, stdout="ready::missing:")

    def test_string_methods(self) -> None:
        source = """
            import "fmt";

            fn main() -> i32 {
                fmt.print(string("hello".len()));
                fmt.print("|" + string("hello".contains("ell")));
                fmt.print("|" + string("hello".starts_with("he")));
                fmt.print("|" + string("hello".ends_with("lo")));
                fmt.print("|" + "  hi  ".trim());
                fmt.print("|" + "hello".to_upper());
                fmt.print("|" + "HELLO".to_lower());
                fmt.print("|" + "hello".replace("l", "L"));

                array<string> parts = "a,b,c".split(",");
                fmt.print("|" + string(parts.len()));

                i32 idx, bool found = "hello".index_of("ll");
                fmt.print("|" + string(idx) + ":" + string(found));

                string sub, err se = "hello".substr(1, 3);
                fmt.print("|" + sub + ":" + se.message());

                array<u8> bs = "AZ".bytes();
                fmt.print("|" + string(bs.len()) + ":" + string(bs[0]) + ":" + string(bs[1]));

                string ch, err ce = "AZ".char_at(1);
                fmt.print("|" + ch + ":" + ce.message());
                return 0;
            }
        """
        self._assert_success(source, stdout="5|true|true|true|hi|HELLO|hello|heLLo|3|2:true|ell:|2:65:90|Z:")

    def test_array_methods_and_indexing(self) -> None:
        source = """
            import "fmt";

            fn main() -> i32 {
                array<i32> a = [1, 2, 3];
                a.push(4);

                i32 popped, err pe = a.pop();
                i32 g, err ge = a.get(1);
                err se = a.set(0, 9);
                array<i32> sl = a.slice(0, 2);
                bool has2 = a.contains(2);
                a[2] = 7;

                fmt.print(string(a.len()) + "|" + string(popped) + ":" + pe.message() + "|" + string(g) + ":" + ge.message() + "|" + se.message() + "|" + string(sl.len()) + "|" + string(has2) + "|" + string(a[2]));
                return 0;
            }
        """
        self._assert_success(source, stdout="3|4:|2:||2|true|7")

    def test_map_methods_and_indexing(self) -> None:
        source = """
            import "fmt";

            fn main() -> i32 {
                map<string, i32> m = {"a": 1};
                err set_err = m.set("b", 2);

                i32 got, bool found = m.get("a");
                i32 removed, bool removed_ok = m.remove("b");
                bool has_a = m.has("a");
                array<string> ks = m.keys();
                array<i32> vs = m.values();

                m["a"] = 9;

                fmt.print(string(m.len()) + "|" + string(got) + ":" + string(found) + "|" + string(removed) + ":" + string(removed_ok) + "|" + string(has_a) + "|" + string(ks.len()) + ":" + string(vs.len()) + "|" + string(m["a"]) + "|" + set_err.message());
                return 0;
            }
        """
        self._assert_success(source, stdout="1|1:true|2:true|true|1:1|9|")

    def test_unsafe_module_basics(self) -> None:
        source = """
            import "fmt";
            import "unsafe";

            fn main() -> i32 {
                unsafe.Buffer b = unsafe.alloc(4);
                b.set(0, u8(65));
                b.set(1, i32(66));
                fmt.print(string(b.len()) + "|" + string(b.get(0)) + "|" + string(b.get(1)) + "|" + string(unsafe.null));
                return 0;
            }
        """
        self._assert_success(source, stdout="4|65|66|null")

    def test_type_enforcement_and_core_failure_paths(self) -> None:
        cases = [
            (
                "var_decl_mismatch",
                'fn main() -> i32 { i32 x = "oops"; return 0; }',
                "type mismatch",
            ),
            (
                "assignment_mismatch",
                'fn main() -> i32 { i32 x = 1; x = "oops"; return 0; }',
                "type mismatch",
            ),
            (
                "fn_param_mismatch",
                'fn id(i32 x) -> i32 { return x; } fn main() -> i32 { return id("x"); }',
                "type mismatch",
            ),
            (
                "fn_signature_mismatch",
                "fn add(i32 a, i32 b) -> i32 { return a + b; } fn main() -> i32 { fn(i32) -> i32 f = add; return 0; }",
                "expected fn with 1 params",
            ),
            (
                "return_type_mismatch",
                'fn bad() -> i32 { return "x"; } fn main() -> i32 { return bad(); }',
                "type mismatch",
            ),
            (
                "tuple_bind_mismatch",
                "fn pair() -> (i32, err) { return (1, ok); } fn main() -> i32 { string s, err e = pair(); return 0; }",
                "type mismatch",
            ),
            (
                "class_field_assign_mismatch",
                'class Box { i32 n; fn init() -> void { self.n = 0; } } fn main() -> i32 { Box b = Box(); b.n = "x"; return 0; }',
                "type mismatch",
            ),
            (
                "array_push_mismatch",
                'fn main() -> i32 { array<i32> a = [1]; a.push("x"); return 0; }',
                "type mismatch",
            ),
            (
                "array_set_mismatch",
                'fn main() -> i32 { array<i32> a = [1]; err e = a.set(0, "x"); return 0; }',
                "type mismatch",
            ),
            (
                "index_assign_mismatch",
                'fn main() -> i32 { array<i32> a = [1]; a[0] = "x"; return 0; }',
                "type mismatch",
            ),
            (
                "map_index_assign_mismatch",
                'fn main() -> i32 { map<string, i32> m = {"k": 1}; m["k"] = "x"; return 0; }',
                "type mismatch",
            ),
            (
                "array_literal_mismatch",
                'fn main() -> i32 { array<i32> a = [1, "x"]; return 0; }',
                "type mismatch",
            ),
            (
                "map_literal_mismatch",
                'fn main() -> i32 { map<i32, string> m = {"x": "y"}; return 0; }',
                "type mismatch",
            ),
            (
                "map_value_literal_mismatch",
                'fn main() -> i32 { map<string, i32> m = {"x": "y"}; return 0; }',
                "type mismatch",
            ),
            (
                "non_bool_if",
                "fn main() -> i32 { if (1) { return 0; } return 0; }",
                "if condition must be bool",
            ),
            (
                "const_reassign",
                "fn main() -> i32 { const i32 X = 1; X = 2; return 0; }",
                "cannot assign to const",
            ),
            (
                "interface_missing_method",
                "interface Foo { fn bar() -> void; } class Bad implements Foo { } fn main() -> i32 { return 0; }",
                "missing method",
            ),
            (
                "interface_unknown",
                "class Bad implements Nonexistent { } fn main() -> i32 { return 0; }",
                "unknown interface",
            ),
            (
                "if_parentheses_required",
                "fn main() -> i32 { if true { return 0; } return 0; }",
                "expected (",
            ),
            (
                "semicolon_required",
                "fn main() -> i32 { i32 x = 1 return 0; }",
                "expected ;",
            ),
            (
                "unsafe_requires_import",
                "fn main() -> i32 { string s = string(unsafe.null); return 0; }",
                "undefined variable",
            ),
            (
                "no_class_inheritance",
                "class A { } class B extends A { } fn main() -> i32 { return 0; }",
                "expected {",
            ),
        ]

        for name, src, err_sub in cases:
            with self.subTest(name=name):
                self._assert_failure(src, err_sub)


    def test_thread_gil_stdlib_safety(self) -> None:
        """GIL ensures stdlib calls from multiple threads don't race."""
        self._assert_success(
            """
            import "fmt";
            import "thread";
            import "math";
            import "mutex";

            class Counter {
                i32 n;
                fn init() -> void { self.n = 0; }
            }

            fn compute(Counter c, Mutex m, i32 id) -> string {
                f64 v = math.sqrt(f64(id * id));
                string s = fmt.sprintf("t%d", id);
                err e1 = m.lock();
                c.n = c.n + 1;
                err e2 = m.unlock();
                return s;
            }

            fn main() -> i32 {
                Counter c = Counter();
                Mutex m, err me = mutex.new();

                Thread t1, err e1 = thread.spawn(compute, c, m, 3);
                Thread t2, err e2 = thread.spawn(compute, c, m, 7);
                Thread t3, err e3 = thread.spawn(compute, c, m, 9);

                string r1, err j1 = t1.join();
                string r2, err j2 = t2.join();
                string r3, err j3 = t3.join();
                err md = m.destroy();

                fmt.print(r1 + "," + r2 + "," + r3);
                fmt.print("|" + string(c.n));
                fmt.print("|" + string(e1 == ok && e2 == ok && e3 == ok));
                fmt.print("|" + string(j1 == ok && j2 == ok && j3 == ok));
                return 0;
            }
            """,
            stdout="t3,t7,t9|3|true|true",
        )

    def test_thread_sleep_releases_gil(self) -> None:
        """thread.sleep releases the GIL so other threads can run concurrently."""
        self._assert_success(
            """
            import "fmt";
            import "thread";
            import "time";

            fn sleeper() -> i32 {
                thread.sleep(30);
                return 1;
            }

            fn main() -> i32 {
                i64 before = time.now();
                Thread t1, err e1 = thread.spawn(sleeper);
                Thread t2, err e2 = thread.spawn(sleeper);
                i32 r1, err j1 = t1.join();
                i32 r2, err j2 = t2.join();
                i64 elapsed = time.since(before);

                fmt.print(string(r1 + r2));
                fmt.print("|" + string(elapsed < i64(500)));
                fmt.print("|" + string(e1 == ok && e2 == ok));
                return 0;
            }
            """,
            stdout="2|true|true",
        )

    def test_thread_join_returns_value(self) -> None:
        """Thread.join returns the spawned function's return value."""
        self._assert_success(
            """
            import "fmt";
            import "thread";

            fn add(i32 a, i32 b) -> i32 { return a + b; }

            fn main() -> i32 {
                Thread th, err e1 = thread.spawn(add, 17, 25);
                i32 result, err e2 = th.join();
                fmt.print(string(result));
                fmt.print("|" + string(e1 == ok && e2 == ok));
                return 0;
            }
            """,
            stdout="42|true",
        )

    def test_thread_join_twice_errors(self) -> None:
        """Joining a thread twice returns an error."""
        self._assert_success(
            """
            import "fmt";
            import "thread";

            fn noop() -> i32 { return 0; }

            fn main() -> i32 {
                Thread th, err e1 = thread.spawn(noop);
                i32 r1, err j1 = th.join();
                i32 r2, err j2 = th.join();
                fmt.print(string(j1 == ok));
                fmt.print("|" + string(j2 != ok));
                return 0;
            }
            """,
            stdout="true|true",
        )


    def test_new_app_project(self) -> None:
        """basl new creates a working application project."""
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            proj = Path(td) / "myapp"
            # Scaffold
            proc = subprocess.run(
                [str(BASL_BIN), "new", str(proj)],
                capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            self.assertIn("created", proc.stdout)

            # Verify files exist
            self.assertTrue((proj / "basl.toml").exists())
            self.assertTrue((proj / "main.basl").exists())
            self.assertTrue((proj / ".gitignore").exists())
            self.assertTrue((proj / "lib").is_dir())
            self.assertTrue((proj / "test").is_dir())

            # Verify basl.toml content
            toml = (proj / "basl.toml").read_text()
            self.assertIn('name = "myapp"', toml)
            self.assertIn('version = "0.1.0"', toml)

            # Verify main.basl runs
            proc = subprocess.run(
                [str(BASL_BIN), str(proj / "main.basl")],
                capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            self.assertEqual(proc.stdout, "hello, world!\n")

    def test_new_lib_project(self) -> None:
        """basl new --lib creates a library project with starter test."""
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            proj = Path(td) / "mylib"
            proc = subprocess.run(
                [str(BASL_BIN), "new", str(proj), "--lib"],
                capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)

            # No main.basl for libraries
            self.assertFalse((proj / "main.basl").exists())
            # Has lib and test starters
            self.assertTrue((proj / "lib" / "mylib.basl").exists())
            self.assertTrue((proj / "test" / "mylib_test.basl").exists())

    def test_new_refuses_nonempty_dir(self) -> None:
        """basl new errors on non-empty directory."""
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            proj = Path(td) / "existing"
            proj.mkdir()
            (proj / "something.txt").write_text("occupied")
            proc = subprocess.run(
                [str(BASL_BIN), "new", str(proj)],
                capture_output=True, text=True,
            )
            self.assertNotEqual(proc.returncode, 0)
            combined = proc.stdout + proc.stderr
            self.assertIn("already exists", combined)

    def test_basl_test_subcommand(self) -> None:
        """basl test discovers and runs _test.basl files."""
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            root = Path(td)
            test_dir = root / "test"
            test_dir.mkdir()
            (test_dir / "math_test.basl").write_text(textwrap.dedent("""
                import "test";
                import "math";
                fn test_sqrt(test.T t) -> void {
                    t.assert(math.sqrt(4.0) == 2.0, "sqrt(4)=2");
                }
                fn test_floor(test.T t) -> void {
                    t.assert(math.floor(3.9) == 3.0, "floor");
                }
            """).strip() + "\n")

            proc = subprocess.run(
                [str(BASL_BIN), "test", "-v", str(test_dir)],
                capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, msg=f"stdout: {proc.stdout}\nstderr: {proc.stderr}")
            self.assertIn("PASS: test_sqrt", proc.stdout)
            self.assertIn("PASS: test_floor", proc.stdout)
            self.assertIn("2 passed", proc.stdout)

    def test_basl_test_filter(self) -> None:
        """basl test -run filters tests by name."""
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            root = Path(td)
            (root / "a_test.basl").write_text(textwrap.dedent("""
                import "test";
                fn test_alpha(test.T t) -> void { t.assert(true, "ok"); }
                fn test_beta(test.T t) -> void { t.assert(true, "ok"); }
                fn test_gamma(test.T t) -> void { t.assert(true, "ok"); }
            """).strip() + "\n")

            proc = subprocess.run(
                [str(BASL_BIN), "test", "-v", "-run", "alpha|gamma", str(root)],
                capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stdout)
            self.assertIn("PASS: test_alpha", proc.stdout)
            self.assertIn("PASS: test_gamma", proc.stdout)
            self.assertNotIn("test_beta", proc.stdout)
            self.assertIn("2 passed", proc.stdout)

    def test_basl_test_failure_reporting(self) -> None:
        """basl test reports failures with messages."""
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            root = Path(td)
            (root / "fail_test.basl").write_text(textwrap.dedent("""
                import "test";
                fn test_good(test.T t) -> void { t.assert(true, "ok"); }
                fn test_bad(test.T t) -> void { t.assert(false, "this broke"); }
            """).strip() + "\n")

            proc = subprocess.run(
                [str(BASL_BIN), "test", str(root)],
                capture_output=True, text=True,
            )
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("FAIL: test_bad", proc.stdout)
            self.assertIn("this broke", proc.stdout)
            self.assertIn("1 passed", proc.stdout)
            self.assertIn("1 failed", proc.stdout)

    def test_embed_single_file(self) -> None:
        """basl embed creates a working module from a single file."""
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            root = Path(td)
            (root / "data.txt").write_text("embed me!")
            proc = subprocess.run(
                [str(BASL_BIN), "embed", str(root / "data.txt"), "-o", str(root / "data_txt.basl")],
                capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)

            # Write a program that uses the embedded module
            (root / "main.basl").write_text(textwrap.dedent("""
                import "fmt";
                import "data_txt";
                fn main() -> i32 {
                    string data, err e = data_txt.bytes();
                    fmt.print(data);
                    fmt.print("|" + string(data_txt.size));
                    fmt.print("|" + data_txt.name);
                    return 0;
                }
            """).strip() + "\n")

            proc = subprocess.run(
                [str(BASL_BIN), str(root / "main.basl")],
                capture_output=True, text=True, cwd=root,
            )
            self.assertEqual(proc.returncode, 0, msg=f"stdout: {proc.stdout}\nstderr: {proc.stderr}")
            self.assertEqual(proc.stdout, "embed me!|9|data.txt")

    def test_embed_directory(self) -> None:
        """basl embed on a directory creates a module with get/list/count."""
        with tempfile.TemporaryDirectory(prefix="basl_it_") as td:
            root = Path(td)
            assets = root / "assets"
            (assets / "sub").mkdir(parents=True)
            (assets / "a.txt").write_text("aaa")
            (assets / "sub" / "b.txt").write_text("bbb")

            proc = subprocess.run(
                [str(BASL_BIN), "embed", str(assets), "-o", str(root / "assets.basl")],
                capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)

            (root / "main.basl").write_text(textwrap.dedent("""
                import "fmt";
                import "assets";
                fn main() -> i32 {
                    fmt.print(string(assets.count));
                    string a, err e1 = assets.get("a.txt");
                    string b, err e2 = assets.get("sub/b.txt");
                    fmt.print("|" + a + "|" + b);
                    string bad, err e3 = assets.get("nope");
                    fmt.print("|" + string(e3 != ok));
                    return 0;
                }
            """).strip() + "\n")

            proc = subprocess.run(
                [str(BASL_BIN), str(root / "main.basl")],
                capture_output=True, text=True, cwd=root,
            )
            self.assertEqual(proc.returncode, 0, msg=f"stdout: {proc.stdout}\nstderr: {proc.stderr}")
            self.assertEqual(proc.stdout, "2|aaa|bbb|true")


if __name__ == "__main__":
    unittest.main(verbosity=2)
