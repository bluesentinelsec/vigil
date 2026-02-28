# Project Structure

BASL enforces a standard directory layout for all projects. This makes tooling predictable — `basl test`, `basl fmt`, `basl build`, and a future package manager all work without configuration.

## Layout

```
myproject/
├── basl.toml              # project manifest
├── main.basl              # entry point (applications only)
├── lib/                   # project library modules
│   ├── utils.basl
│   └── http/
│       ├── router.basl
│       └── middleware.basl
├── test/                  # test files (*_test.basl)
│   ├── utils_test.basl
│   └── http/
│       └── router_test.basl
├── deps/                  # downloaded dependencies (gitignored)
└── assets/                # embedded files (optional)
```

## `basl.toml`

Minimal project manifest:

```toml
name = "myproject"
version = "0.1.0"

[deps]
json_schema = "1.2.0"
```

No build flags, no source paths, no output config. The directory structure is the config.

## `basl new`

Scaffolds a new project:

```sh
basl new myapp              # application (with main.basl)
basl new mylib --lib        # library (no main.basl)
```

### Application project

```sh
$ basl new myapp
created myapp/
  basl.toml
  main.basl
  lib/
  test/
  .gitignore
```

`main.basl`:
```c
import "fmt";

fn main() -> i32 {
    fmt.println("hello, world!");
    return 0;
}
```

### Library project

```sh
$ basl new mylib --lib
created mylib/
  basl.toml
  lib/mylib.basl
  test/mylib_test.basl
  .gitignore
```

`lib/mylib.basl`:
```c
/// mylib library module.

pub fn hello() -> string {
    return "hello from mylib";
}
```

`test/mylib_test.basl`:
```c
import "t";
import "mylib";

fn test_hello() -> void {
    t.assert(mylib.hello() == "hello from mylib", "hello should match");
}
```

## Import Resolution

Imports are resolved in order:

1. **Stdlib** — built-in modules (`fmt`, `os`, `math`, etc.)
2. **`lib/`** — project source modules
3. **`deps/`** — downloaded dependencies

```c
import "fmt";              // stdlib
import "utils";            // lib/utils.basl
import "http/router";      // lib/http/router.basl
import "json_schema";      // deps/json_schema/
```

No relative paths (`../`) needed. No path configuration.

## How Tools Use the Structure

| Command | Behavior |
|---------|----------|
| `basl new` | Scaffolds the standard layout |
| `basl test` | Runs everything in `test/` |
| `basl fmt ./...` | Formats `lib/`, `test/`, and `main.basl` |
| `basl embed` | Output conventionally goes to `lib/` |
| `basl package` | Entry point is `main.basl` |
| Future `basl deps` | Downloads into `deps/`, reads `basl.toml` |

## Conventions

- One entry point per project: `main.basl` at the root.
- Library code goes in `lib/`. Subdirectories map to import paths.
- Tests go in `test/`. Mirror the `lib/` structure.
- Dependencies go in `deps/`. Always gitignored.
- `basl.toml` is the single source of truth for project metadata.
