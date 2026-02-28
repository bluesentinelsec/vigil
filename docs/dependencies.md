# Dependency Management

BASL uses git repositories as the distribution mechanism for libraries. No central registry — dependencies are fetched directly from git URLs.

## Commands

### `basl get` — add a dependency

```sh
basl get <git-url>[@<tag|commit>]
```

Clones the repository into `deps/<name>/`, adds it to `basl.toml`, and writes the resolved commit to `basl.lock`.

```sh
basl get https://github.com/user/json_schema@v1.2.0
basl get https://github.com/user/utils              # latest
basl get https://github.com/user/crypto@abc123f      # specific commit
```

### `basl upgrade` — upgrade dependencies

```sh
basl upgrade                          # upgrade all to latest
basl upgrade <name>                   # upgrade one to latest
basl upgrade <name>@v2.0.0           # upgrade to specific version
```

Fetches from remote, updates `basl.toml` and `basl.lock`.

### `basl remove` — remove a dependency

```sh
basl remove <name>
```

Removes from `basl.toml`, deletes from `deps/`, and removes from `basl.lock`.

### `basl deps` — sync dependencies

```sh
basl deps
```

Reads `basl.toml` and ensures `deps/` matches. Run this after cloning a project:

```sh
git clone https://github.com/someone/cool_app
cd cool_app
basl deps
```

Also removes stale entries from `deps/` that are no longer in `basl.toml`.

## Files

| File | Purpose | Commit? |
|------|---------|---------|
| `basl.toml` | What you want — dependency names, URLs, versions | Yes |
| `basl.lock` | What you got — exact commit hashes for reproducibility | Yes |
| `deps/` | Local checkout of dependencies | No (gitignored) |

### `basl.toml`

```toml
name = "myapp"
version = "0.1.0"

[deps]
json_schema = { git = "https://github.com/user/json_schema", tag = "v1.2.0" }
http_router = { git = "https://github.com/user/http_router", tag = "v0.5.0" }
```

### `basl.lock`

```toml
[json_schema]
git = "https://github.com/user/json_schema"
tag = "v1.2.0"
commit = "abc123f..."

[http_router]
git = "https://github.com/user/http_router"
tag = "v0.5.0"
commit = "def456a..."
```

`basl deps` uses the lock file commit hash for reproducible builds. `basl upgrade` updates both the lock and the manifest.

## Importing Dependencies

Dependencies follow the standard project layout. A dependency named `json_schema` is expected to have its public modules in `lib/`:

```
deps/json_schema/
├── basl.toml
├── lib/
│   └── json_schema.basl
└── test/
    └── json_schema_test.basl
```

Import by name:

```c
import "json_schema";

fn main() -> i32 {
    json_schema.validate(data);
    return 0;
}
```

The module resolver checks in order:
1. Stdlib built-in modules
2. `lib/` (project source)
3. `deps/<name>/lib/` (dependencies)

## Name Derivation

The dependency name comes from the last path segment of the git URL:

| URL | Name |
|-----|------|
| `https://github.com/user/json_schema` | `json_schema` |
| `https://github.com/user/http-router` | `http_router` |
| `https://github.com/user/basl-utils.git` | `basl_utils` |

## Workflow

### Starting a new project with dependencies

```sh
basl new myapp
cd myapp
basl get https://github.com/user/json_schema@v1.2.0
basl get https://github.com/user/http_router@v0.5.0
# Edit main.basl to use them
basl main.basl
```

### Cloning and building someone else's project

```sh
git clone https://github.com/someone/cool_app
cd cool_app
basl deps
basl main.basl
```

### Upgrading a dependency

```sh
basl upgrade json_schema          # latest tag
basl upgrade json_schema@v2.0.0  # specific version
basl test                         # verify nothing broke
```

### Publishing a library

No special publish step. Push to a git repository with:
- `basl.toml` at the root
- Public modules in `lib/`
- Tests in `test/`
- Semantic version tags (`v1.0.0`, `v1.1.0`, etc.)

Others install with:
```sh
basl get https://github.com/you/your_lib@v1.0.0
```
