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


class RealProgramPatternsTest(unittest.TestCase):
    """Tests for patterns that arise in real programs."""

    # -- error kind routing --

    def test_error_kind_switch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn lookup(string key) -> (string, err) {
                    if (key == "") {
                        return ("", err("empty key", err.arg));
                    }
                    if (key == "missing") {
                        return ("", err("not found", err.not_found));
                    }
                    return ("value_" + key, ok);
                }

                fn main() -> i32 {
                    string v, err e = lookup("missing");
                    if (e == ok) { return 1; }
                    switch (e.kind()) {
                        case err.not_found:
                            return 10;
                        case err.arg:
                            return 20;
                        default:
                            return 30;
                    }
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 10, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- cross-module classes, interfaces, enums --

    def test_cross_module_interface_dispatch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "types.basl": """
                    pub enum Status {
                        Active,
                        Inactive
                    }

                    pub interface Describable {
                        fn describe() -> string;
                    }

                    pub class User implements Describable {
                        pub string name;
                        pub Status status;

                        fn init(string name, Status status) -> void {
                            self.name = name;
                            self.status = status;
                        }

                        fn describe() -> string {
                            return f"User({self.name})";
                        }
                    }
                """,
                "main.basl": """
                    import "types";

                    fn print_desc(types.Describable d) -> string {
                        return d.describe();
                    }

                    fn main() -> i32 {
                        types.User u = types.User("alice", types.Status.Active);
                        string desc = print_desc(u);
                        if (desc == "User(alice)") {
                            return 0;
                        }
                        return 1;
                    }
                """,
            })
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- nested generic types (>> token splitting) --

    def test_nested_array_type(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    array<array<i32>> grid = [[1, 2], [3, 4], [5, 6]];
                    i32 total = 0;
                    for row in grid {
                        for val in row {
                            total += val;
                        }
                    }
                    return total;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 21, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_map_with_nested_array_value(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    map<string, array<i32>> data = {"a": [1, 2], "b": [3, 4]};
                    array<i32> a_vals, bool found = data.get("a");
                    if (!found) { return 1; }
                    return a_vals[0] + a_vals[1];
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 3, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_triple_nested_array(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    array<array<array<i32>>> deep = [[[1, 2], [3]], [[4]]];
                    return deep.len();
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- nested control flow --

    def test_guard_in_loop_with_continue(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn maybe(i32 x) -> (i32, err) {
                    if (x == 2) {
                        return (0, err("skip", err.arg));
                    }
                    return (x * 10, ok);
                }

                fn main() -> i32 {
                    i32 total = 0;
                    for (i32 i = 0; i < 5; i++) {
                        guard i32 val, err e = maybe(i) {
                            continue;
                        }
                        total += val;
                    }
                    return total;
                }
            """})
            # i=0->0, i=1->10, i=2->skip, i=3->30, i=4->40 = 80
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 80, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_defer_with_break(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                i32 counter = 0;

                fn bump() -> void {
                    counter += 1;
                }

                fn work() -> i32 {
                    defer bump();
                    for (i32 i = 0; i < 10; i++) {
                        if (i == 3) { break; }
                    }
                    return counter;
                }

                fn main() -> i32 {
                    i32 result = work();
                    return result + counter;
                }
            """})
            # work() returns 0, defer runs -> counter=1, main returns 0+1=1
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 1, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_defer_guard_forin_combined(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                i32 cleanup_count = 0;

                fn do_cleanup() -> void {
                    cleanup_count += 1;
                }

                fn maybe_val(i32 x) -> (i32, err) {
                    if (x < 0) {
                        return (0, err("negative", err.arg));
                    }
                    return (x, ok);
                }

                fn process(array<i32> items) -> i32 {
                    defer do_cleanup();
                    i32 total = 0;
                    for val in items {
                        guard i32 v, err e = maybe_val(val) {
                            continue;
                        }
                        total += v;
                    }
                    return total;
                }

                fn main() -> i32 {
                    array<i32> data = [1, -2, 3, -4, 5];
                    i32 result = process(data);
                    if (cleanup_count != 1) { return 99; }
                    return result;
                }
            """})
            # 1+3+5=9, cleanup_count=1
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 9, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- closures capturing loop variables --

    def test_closure_captures_loop_variable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    array<fn() -> i32> fns = [];
                    for (i32 i = 0; i < 3; i++) {
                        i32 captured = i;
                        fn() -> i32 f = fn() -> i32 {
                            return captured;
                        };
                        fns.push(f);
                    }
                    fn() -> i32 f0, err e0 = fns.get(0);
                    fn() -> i32 f2, err e2 = fns.get(2);
                    return f0() + f2();
                }
            """})
            # f0()=0, f2()=2, total=2
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- compound expressions --

    def test_ternary_in_function_args(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn add(i32 a, i32 b) -> i32 {
                    return a + b;
                }

                fn main() -> i32 {
                    bool flag = true;
                    return add(flag ? 10 : 20, flag ? 1 : 2);
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 11, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_chained_string_methods(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    string result = "  Hello World  ".trim().to_lower().replace("world", "basl");
                    if (result == "hello basl") {
                        return 0;
                    }
                    return 1;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_map_compound_index_assignment(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    map<string, i32> m = {"a": 1, "b": 2};
                    m["a"] += 10;
                    m["b"] *= 3;
                    return m["a"] + m["b"];
                }
            """})
            # 1+10=11, 2*3=6, 11+6=17
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 17, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_nested_indexing(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    array<array<i32>> grid = [];
                    array<i32> row1 = [1, 2, 3];
                    array<i32> row2 = [4, 5, 6];
                    grid.push(row1);
                    grid.push(row2);
                    return grid[0][1] + grid[1][2];
                }
            """})
            # grid[0][1]=2, grid[1][2]=6, 2+6=8
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 8, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_fstring_with_method_call(self) -> None:
        with tempfile.TemporaryDirectory(prefix="basl_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.basl": """
                fn main() -> i32 {
                    string name = "  alice  ";
                    string msg = f"hello {name.trim()}!";
                    if (msg == "hello alice!") {
                        return 0;
                    }
                    return 1;
                }
            """})
            result = run_basl(root, "main.basl")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
