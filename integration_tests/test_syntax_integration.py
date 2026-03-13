#!/usr/bin/env python3

import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def resolve_basl_command() -> list[str]:
    configured_bin = os.environ.get("BASL_BIN")
    native_bin = REPO_ROOT / "build" / ("basl.exe" if os.name == "nt" else "basl")
    wasm_bin = REPO_ROOT / "build" / "basl.js"

    if configured_bin:
        return [configured_bin]
    if native_bin.exists():
        return [str(native_bin)]
    if wasm_bin.exists():
        return [os.environ.get("EMSDK_NODE", "node"), str(wasm_bin)]

    raise FileNotFoundError("could not locate BASL CLI executable in build output")


def write_sources(root: Path, sources: dict[str, str]) -> None:
    for relative_path, text in sources.items():
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(text).strip() + "\n", encoding="utf-8")


def run_basl(root: Path, entrypoint: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [*resolve_basl_command(), str(root / entrypoint)],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        check=False,
    )


class SyntaxIntegrationTest(unittest.TestCase):
    """Existing tests from PR #88."""

    def test_import_alias_and_default_nested_alias(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(
                root,
                {
                    "lib/math.basl": """
                        pub fn double(i32 x) -> i32 {
                            return x * 2;
                        }
                    """,
                    "shared/utils.basl": """
                        pub fn answer() -> i32 {
                            return 9;
                        }
                    """,
                    "main.basl": """
                        import "lib/math" as ops;
                        import "shared/utils";

                        fn main() -> i32 {
                            return ops.double(utils.answer());
                        }
                    """,
                },
            )

            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 18, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_numeric_and_character_literals(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(
                root,
                {
                    "main.basl": """
                        fn main() -> i32 {
                            i32 total = 0x10 + 0b11 + 0o7;
                            string letter = 'A';
                            string newline = '\\n';

                            if (letter == "A" && newline.len() == 1) {
                                return total;
                            }
                            return 0;
                        }
                    """,
                },
            )

            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 26, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_function_typed_globals_and_imported_collection_globals(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(
                root,
                {
                    "lib/state.basl": """
                        pub array<i32> nums = [];

                        pub fn seed() -> void {
                            nums.push(3);
                            nums.push(4);
                        }
                    """,
                    "main.basl": """
                        import "lib/state" as st;

                        fn twice(i32 value) -> i32 {
                            return value * 2;
                        }

                        fn(i32) -> i32 op = twice;
                        fn any = twice;

                        fn main() -> i32 {
                            st.nums = [];
                            st.seed();
                            st.nums[1] += 5;
                            return op(st.nums[0]) + op(st.nums[1]);
                        }
                    """,
                },
            )

            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 24, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_multiple_interface_dispatch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(
                root,
                {
                    "main.basl": """
                        interface Named {
                            fn name() -> string;
                        }

                        interface Sized {
                            fn size() -> i32;
                        }

                        class Box implements Named, Sized {
                            pub string label;
                            pub i32 count;

                            fn init(string label, i32 count) -> void {
                                self.label = label;
                                self.count = count;
                            }

                            fn name() -> string {
                                return self.label;
                            }

                            fn size() -> i32 {
                                return self.count;
                            }
                        }

                        fn describe(Named named, Sized sized) -> i32 {
                            if (named.name() == "ok") {
                                return sized.size();
                            }
                            return 0;
                        }

                        fn main() -> i32 {
                            Box box = Box("ok", 12);
                            return describe(box, box);
                        }
                    """,
                },
            )

            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 12, msg=result.stderr)
            self.assertEqual(result.stderr, "")


class DocsConformanceTest(unittest.TestCase):
    """Tests derived from docs/syntax.md conformance sweep."""

    # -- closures and function values --

    def test_closure_captures_local(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 factor = 3;
                    fn(i32) -> i32 scale = fn(i32 x) -> i32 {
                        return x * factor;
                    };
                    return scale(7);
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 21, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_iife(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 result = fn() -> i32 {
                        return 42;
                    }();
                    return result;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 42, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_local_named_function(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    fn helper(i32 x) -> i32 {
                        return x * 2;
                    }
                    return helper(11);
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 22, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- error flow and guard --

    def test_error_flow(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn risky(bool fail) -> (i32, err) {
                    if (fail) {
                        return (0, err("bad", err.arg));
                    }
                    return (42, ok);
                }

                fn main() -> i32 {
                    i32 v1, err e1 = risky(false);
                    if (e1 != ok) { return 1; }
                    if (v1 != 42) { return 2; }

                    i32 v2, err e2 = risky(true);
                    if (e2 == ok) { return 3; }

                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_guard_statement(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn fallible(i32 x) -> (i32, err) {
                    if (x == 0) {
                        return (0, err("zero", err.arg));
                    }
                    return (x * 2, ok);
                }

                fn main() -> i32 {
                    guard i32 val, err e = fallible(0) {
                        return 99;
                    }
                    return val;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 99, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_fallible_class_init(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                class Conn {
                    pub string host;

                    fn init(string host) -> err {
                        if (host == "") {
                            return err("empty host", err.arg);
                        }
                        self.host = host;
                        return ok;
                    }
                }

                fn main() -> i32 {
                    Conn c1, err e1 = Conn("localhost");
                    if (e1 != ok) { return 1; }
                    if (c1.host != "localhost") { return 2; }

                    Conn c2, err e2 = Conn("");
                    if (e2 == ok) { return 3; }

                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- loops, break, continue --

    def test_for_while_forin_break_continue(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 total = 0;

                    for (i32 i = 0; i < 5; i++) {
                        if (i == 3) { continue; }
                        total += i;
                    }

                    i32 w = 0;
                    while (w < 3) {
                        total += 10;
                        w++;
                    }

                    array<i32> nums = [10, 20, 30];
                    for val in nums {
                        total += val;
                    }

                    map<string, i32> m = {"x": 1, "y": 2};
                    for key, val in m {
                        total += val;
                    }

                    for (i32 j = 0; j < 100; j++) {
                        if (j == 2) { break; }
                        total += 100;
                    }

                    return total;
                }
            """})
            # 0+1+2+4=7, +30 while, +60 for-in array, +3 for-in map, +200 break loop = 300
            # exit codes are modulo 256 → 44
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 44, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- switch and enum --

    def test_switch_with_enum(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                enum Color {
                    Red,
                    Green,
                    Blue
                }

                fn main() -> i32 {
                    Color c = Color.Green;
                    switch (c) {
                        case Color.Red:
                            return 1;
                        case Color.Green:
                            return 2;
                        case Color.Blue:
                            return 3;
                        default:
                            return 0;
                    }
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_switch_multi_case(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 x = 3;
                    switch (x) {
                        case 1:
                            return 10;
                        case 2, 3:
                            return 20;
                        default:
                            return 30;
                    }
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 20, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_enum_with_explicit_values(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                enum HttpStatus {
                    Ok = 200,
                    NotFound = 404
                }

                fn main() -> i32 {
                    HttpStatus s = HttpStatus.NotFound;
                    switch (s) {
                        case HttpStatus.NotFound:
                            return 44;
                        default:
                            return 0;
                    }
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 44, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- defer --

    def test_defer_runs_after_return(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                i32 counter = 0;

                fn bump() -> void {
                    counter += 1;
                }

                fn work() -> i32 {
                    defer bump();
                    defer bump();
                    return counter;
                }

                fn main() -> i32 {
                    i32 result = work();
                    return result + counter;
                }
            """})
            # work() returns 0 (defers haven't run yet), then defers run: counter=2
            # main returns 0 + 2 = 2
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- string built-ins --

    def test_string_builtins(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    string s = "  Hello World  ";
                    string trimmed = s.trim();
                    if (trimmed != "Hello World") { return 1; }
                    if (!trimmed.contains("World")) { return 2; }
                    if (!trimmed.starts_with("Hello")) { return 3; }
                    if (!trimmed.ends_with("World")) { return 4; }
                    if (trimmed.to_upper() != "HELLO WORLD") { return 5; }
                    if (trimmed.to_lower() != "hello world") { return 6; }
                    if (trimmed.replace("World", "BASL") != "Hello BASL") { return 7; }
                    array<string> parts = trimmed.split(" ");
                    if (parts.len() != 2) { return 8; }
                    i32 idx, bool found = trimmed.index_of("World");
                    if (!found) { return 9; }
                    if (idx != 6) { return 10; }
                    string sub, err e1 = trimmed.substr(0, 5);
                    if (e1 != ok) { return 11; }
                    if (sub != "Hello") { return 12; }
                    array<u8> bytes = trimmed.bytes();
                    if (bytes.len() != 11) { return 13; }
                    string ch, err e2 = trimmed.char_at(0);
                    if (e2 != ok) { return 14; }
                    if (ch != "H") { return 15; }
                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- array built-ins --

    def test_array_builtins(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    array<i32> a = [1, 2, 3];
                    a.push(4);
                    if (a.len() != 4) { return 1; }
                    i32 popped, err e1 = a.pop();
                    if (e1 != ok) { return 2; }
                    if (popped != 4) { return 3; }
                    i32 val, err e2 = a.get(1);
                    if (e2 != ok) { return 4; }
                    if (val != 2) { return 5; }
                    err e3 = a.set(0, 99);
                    if (e3 != ok) { return 6; }
                    if (a[0] != 99) { return 7; }
                    array<i32> sliced = a.slice(1, 3);
                    if (sliced.len() != 2) { return 8; }
                    if (!a.contains(99)) { return 9; }
                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- map built-ins --

    def test_map_builtins(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    map<string, i32> m = {"a": 1, "b": 2};
                    if (m.len() != 2) { return 1; }
                    i32 val, bool found = m.get("a");
                    if (!found) { return 2; }
                    if (val != 1) { return 3; }
                    if (!m.has("b")) { return 4; }
                    err e = m.set("c", 3);
                    if (e != ok) { return 5; }
                    if (m.len() != 3) { return 6; }
                    i32 removed, bool had = m.remove("b");
                    if (!had) { return 7; }
                    if (removed != 2) { return 8; }
                    array<string> keys = m.keys();
                    if (keys.len() != 2) { return 9; }
                    array<i32> vals = m.values();
                    if (vals.len() != 2) { return 10; }
                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- type conversions --

    def test_type_conversions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 x = 42;
                    i64 big = i64(x);
                    f64 fval = f64(x);
                    string s = string(x);
                    if (s != "42") { return 1; }
                    if (big != i64(42)) { return 2; }
                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- wider integer types --

    def test_wider_integer_types(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    u8 byte = u8(255);
                    u32 mid = u32(1000);
                    u64 big = u64(999);
                    i64 signed_big = i64(500);
                    i32 result = i32(byte) + i32(mid) + i32(big) + i32(signed_big);
                    if (result != 2754) { return 1; }
                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- constants --

    def test_constants(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                const i32 MAX = 100;
                const string LABEL = "test";

                fn main() -> i32 {
                    const i32 LOCAL_MAX = 50;
                    if (LABEL != "test") { return 1; }
                    if (MAX != 100) { return 2; }
                    if (LOCAL_MAX != 50) { return 3; }
                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- ternary --

    def test_ternary_expression(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 a = 10;
                    i32 b = 20;
                    i32 max = a > b ? a : b;
                    return max;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 20, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- bitwise operations --

    def test_bitwise_operations(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 a = 0xFF;
                    i32 b = 0x0F;
                    i32 result = (a & b) | 0x10;
                    return result;
                }
            """})
            # 0xFF & 0x0F = 0x0F = 15, 15 | 0x10 = 31
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 31, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- f-strings --

    def test_fstring_interpolation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 x = 5;
                    i32 y = 10;
                    string msg = f"sum={x + y}";
                    if (msg != "sum=15") { return 1; }
                    return 0;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- classes --

    def test_class_construction_and_methods(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                class Counter {
                    pub i32 value;

                    fn init(i32 start) -> void {
                        self.value = start;
                    }

                    pub fn add(i32 n) -> void {
                        self.value += n;
                    }
                }

                fn main() -> i32 {
                    Counter c = Counter(10);
                    c.add(5);
                    return c.value;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 15, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- compound assignment --

    def test_compound_assignment_and_increment(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    i32 x = 10;
                    x += 5;
                    x -= 1;
                    x *= 2;
                    x /= 7;
                    x %= 3;
                    x++;
                    return x;
                }
            """})
            # 10+5=15, -1=14, *2=28, /7=4, %3=1, ++=2
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- pub enforcement across modules --

    def test_pub_enforcement(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "lib.basl": """
                    fn private_fn() -> i32 {
                        return 1;
                    }

                    pub fn public_fn() -> i32 {
                        return 2;
                    }
                """,
                "main.basl": """
                    import "lib";

                    fn main() -> i32 {
                        return lib.private_fn();
                    }
                """,
            })
            result = run_basl(root, "main.basl")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not public", result.stderr)


if __name__ == "__main__":
    unittest.main()
