# Project Structure

BASL enforces a standard directory layout for all projects. This makes tooling predictable — `basl test`, `basl fmt`, `basl package`, and `basl get` all work without configuration.

## Layout

```
myproject/
├── basl.toml              # project manifest
├── basl.lock              # locked dependency versions (auto-generated)
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
│   └── github.com/
│       └── user/
│           └── repo/
└── assets/                # embedded files (optional)
```

## `basl.toml`

Minimal project manifest:

```toml
name = "myproject"
version = "0.1.0"

[deps]
"github.com/user/json" = "v1.2.0"
"github.com/user/http" = "main"
```

No build flags, no source paths, no output config. The directory structure is the config.

## `basl get`

Manage dependencies using git for distribution:

```sh
basl get                              # sync all deps from basl.toml
basl get github.com/user/repo         # install latest
basl get github.com/user/repo@v1.0.0  # install specific version/tag
basl get github.com/user/repo@main    # install branch
```

Dependencies are cloned to `deps/` and can be imported by their full path:

```basl
import "github.com/user/json";

fn main() -> i32 {
    json.parse("{}");
    return 0;
}
```

## `basl new`

Scaffolds a new project:

```sh
basl new myapp              # application (with main.basl)
basl new mylib --lib        # library (no main.basl)
```

### Application project

```sh
$ basl new myapp
created myapp
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
created mylib
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
import "test";
import "mylib";

fn test_hello(test.T t) -> void {
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
| `basl fmt ./...` | Formats all `.basl` files under the target tree |
| `basl embed` | Output conventionally goes to `lib/` |
| `basl package` | Packages an app (`main.basl`) or a library bundle |
| `basl deps` | Downloads into `deps/`, reads `basl.toml` |

## Conventions

- One entry point per project: `main.basl` at the root.
- Library code goes in `lib/`. Subdirectories map to import paths.
- Tests go in `test/`. Mirror the `lib/` structure.
- Dependencies go in `deps/`. Always gitignored.
- `basl.toml` is the single source of truth for project metadata.
