#!/usr/bin/env python3

import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def resolve_basl_command() -> list[str]:
    native_bin = REPO_ROOT / "build" / ("basl.exe" if os.name == "nt" else "basl")
    wasm_bin = REPO_ROOT / "build" / "basl.js"

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
                            string newline = '\n';

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


if __name__ == "__main__":
    unittest.main()
