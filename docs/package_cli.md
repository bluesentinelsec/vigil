# BASL `package` CLI

`basl package` creates a single-file native executable for a BASL program.

The produced file is a copy of the current `basl` interpreter binary with a bundled BASL application payload appended to it. The bundled executable can then run the packaged BASL program directly.

This is intentionally a local packaging feature:

1. no cross-compilation support
2. no source-to-machine-code compilation
3. no separate runtime install required on the target machine

## Quick Start

Package an app:

```sh
cd myapp
basl package
```

Run the packaged app:

```sh
./myapp arg1 arg2
```

Inspect a packaged app:

```sh
basl package --inspect ./myapp
```

## Commands

### Build a packaged executable

```sh
basl package [-o output] [--path dir] [<entry.basl|project-dir>]
```

Examples:

```sh
basl package
basl package ./myapp
basl package ./examples/app.basl
basl package -o dist/myapp ./cmd/main.basl
basl package --path ./lib ./app/main.basl
```

Behavior:

1. resolves the packaging target
2. reads the entry `.basl` file
3. if the target is omitted, uses the current working directory
4. if the target is a BASL project root (contains `basl.toml`), defaults to `main.basl`
5. automatically resolves project imports from `lib/` and `deps/`
6. resolves reachable BASL source imports
7. rewrites bundled BASL-file imports to internal package paths
8. stores the rewritten BASL files in an appended payload
9. writes a new executable at the output path

If `-o` is omitted:

1. project mode uses the project directory name
2. file mode uses the entry filename without the `.basl` suffix

Example:

```sh
cd myproject
basl package
```

Produces:

```sh
./myproject
```

If the project is a library project (no `main.basl` at the root), packaging fails. `basl package` only builds runnable applications.

### Inspect a packaged executable

```sh
basl package --inspect <binary>
```

This reads the packaged payload from an existing executable and prints:

1. the fixed bundled entrypoint
2. the list of bundled BASL files

Example output:

```text
ENTRY
  entry.basl

FILES
  entry.basl
  pkg/mod001.basl
  pkg/mod002.basl
```

This is useful for:

1. confirming the file is actually a BASL packaged binary
2. seeing what BASL modules were embedded
3. debugging import resolution during packaging

## How It Works

The current implementation does not rebuild the interpreter or compile generated Go code.

Instead, it:

1. copies the current `basl` executable
2. appends a zip archive containing bundled BASL source files
3. appends an 8-byte little-endian payload length
4. appends the trailer magic string `BASLPKG1`

At startup, the executable:

1. checks its own trailer for `BASLPKG1`
2. if no trailer is present, behaves like a normal `basl` CLI
3. if the trailer is present, loads the bundled BASL files into memory
4. executes the bundled entry script immediately

This means the same runtime binary can act as:

1. the normal `basl` interpreter
2. a packaged BASL application

depending only on whether the payload trailer is present.

## Entrypoint Convention

The packaged bundle always stores the entry script at the internal path:

```text
entry.basl
```

This path is fixed by the packager.

At runtime, the packaged executable always:

1. reads `entry.basl` from the appended bundle
2. lexes and parses it
3. executes it as the program entry

That script must define:

```c
fn main() -> i32 {
    return 0;
}
```

just like a normal BASL program.

### Project mode

In a BASL project, `basl package` treats the project root `main.basl` as the application entrypoint.

That means these are equivalent from the project root:

```sh
basl package
basl package .
basl package ./main.basl
```

assuming `basl.toml` and `main.basl` are present.

## Import Bundling

The packager includes BASL source files reachable from the entrypoint through BASL `import` declarations.

In a BASL project, this matches the standard project layout:

1. `main.basl` at the project root
2. project modules in `lib/`
3. dependency modules in `deps/`

### What is bundled

Bundled:

1. the entry `.basl` file
2. imported BASL source files resolved from disk

Not bundled:

1. native builtin modules such as `fmt`, `os`, `json`, `file`, `thread`, etc.
2. arbitrary runtime data files
3. files opened manually at runtime unless you ship them separately

Builtin modules are already part of the interpreter binary, so they do not need to be copied into the payload.

### Import rewriting

Bundled BASL modules are rewritten to internal synthetic import paths like:

```text
pkg/mod001
pkg/mod002
```

This is an internal packaging detail. The packager rewrites imports automatically so the bundled app continues to work without the original source tree.

If an imported BASL module used the default alias, the packager preserves the effective alias by writing an explicit `as` alias when needed.

## Search Paths

`--path` adds additional directories to search when resolving BASL source imports during packaging.

Example:

```sh
basl package --path ./lib --path ./vendor ./app/main.basl
```

Resolution order:

In a BASL project:

1. `lib/`
2. `deps/`
3. each `--path` directory, in the order provided

Outside a BASL project:

1. the entry file directory
2. each `--path` directory, in the order provided

`--path` is still useful in project mode for non-standard supplemental search locations, but normal project imports should not require it.

This affects packaging-time source resolution only.

It does not change how the packaged executable resolves imports at runtime, because packaged BASL source imports are already rewritten to internal embedded paths.

## Script Arguments

Arguments passed to the packaged executable are forwarded to the BASL runtime exactly like normal script execution.

Example:

```sh
./myapp alpha beta
```

Inside BASL:

```c
import "os";

fn main() -> i32 {
    array<string> argv = os.args();
    // argv[0] == "alpha"
    // argv[1] == "beta"
    return 0;
}
```

## What This Feature Is Good For

Good fit:

1. distributing CLI-like BASL programs as a single file
2. shipping internal tools without requiring users to manage `.basl` source trees
3. packaging apps that rely on BASL source imports plus builtin native modules

Not a good fit:

1. cross-platform build pipelines
2. native-code compilation or optimization
3. asset-heavy applications that need embedded non-BASL resources

## Limitations

Current limitations:

1. No cross-compilation support.
   - Packaging is for the current host platform only.

2. No asset bundling.
   - Non-BASL files are not embedded.

3. No dependency manifest.
   - The bundle is just the rewritten BASL files plus the fixed entrypoint convention.

4. No signature or integrity validation beyond the simple trailer format.

5. Packaging depends on the current `basl` binary.
   - The packaged output is a copy of whatever `basl` executable ran the `package` command.

## Troubleshooting

### `module "..." not found for packaging`

Cause:

In a BASL project:

1. the imported BASL file is not under `lib/`
2. it is not under `deps/`
3. it is not in any `--path` directory

Outside a BASL project:

1. the imported BASL file is not reachable from the entry directory
2. it is not in any `--path` directory

Fix:

1. verify the import path maps to a `.basl` file
2. in a project, verify the module is under `lib/` or `deps/`
3. add the correct `--path` only if you are intentionally using an extra search root

### `project ... has no main.basl to package`

Cause:

1. you ran `basl package` against a library project
2. the project root is missing its application entrypoint

Fix:

1. package an application project instead
2. pass an explicit `.basl` file if you want file mode

### `... is not a BASL packaged binary`

Cause:

1. the file was not produced by `basl package`
2. the trailer is missing or corrupted

Fix:

1. rebuild the packaged executable
2. run `basl package --inspect` on the correct file

### The packaged app cannot find a runtime data file

Cause:

1. `basl package` only bundles BASL source files

Fix:

1. ship the data file separately
2. use a stable runtime path convention in your app

## Recommended Workflow

For local release testing:

```sh
make build
cd myapp
basl package -o dist/myapp
basl package --inspect ./dist/myapp
./dist/myapp smoke-test
```

This verifies:

1. the interpreter binary is current
2. the project-layout app packages correctly
3. the bundle contains the expected files
4. the packaged app runs end-to-end
