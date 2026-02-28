# BASL Help System

The BASL CLI now has a layered help system.

Instead of a minimal `--help` banner, BASL provides:

1. a command overview
2. detailed help for each command
3. topic help for common concepts
4. `--help` support on subcommands

## Quick Start

Show the top-level command overview:

```sh
basl --help
```

or:

```sh
basl help
```

Show detailed help for one command:

```sh
basl help package
basl help fmt
basl help doc
basl help editor
```

Show help for one concept:

```sh
basl help run
basl help imports
basl help packaging
```

Show help directly from a subcommand:

```sh
basl fmt --help
basl doc --help
basl test --help
basl package --help
basl embed --help
```

## Top-Level Help

`basl --help` shows:

1. a short description of BASL
2. general usage forms
3. available commands with one-line summaries
4. global options
5. built-in help topics
6. common examples

This is the best starting point if you are not sure which command you need.

## Command Help

Use:

```sh
basl help <command>
```

Supported command help pages:

1. `fmt`
2. `doc`
3. `test`
4. `package`
5. `editor`
6. `embed`
7. `help`

Each command help page includes:

1. summary
2. usage forms
3. descriptive notes
4. examples
5. related commands/topics

Example:

```sh
basl help package
```

This explains:

1. how standalone packaging works
2. `--inspect`
3. common packaging examples

Example:

```sh
basl help editor
```

This explains:

1. how to list supported editor targets
2. how installation and removal work
3. what `--home` and `--force` do

## Topic Help

Use:

```sh
basl help <topic>
```

Current built-in topics:

1. `run`
2. `imports`
3. `packaging`

These are not executable commands. They are short documentation pages for common concepts.

### `run`

Explains:

1. how BASL runs scripts from the CLI
2. how script arguments are forwarded
3. how to use `--` to stop CLI option parsing

### `imports`

Explains:

1. builtin module resolution
2. file-backed module resolution
3. the effect of `--path`
4. `pub` export visibility

### `packaging`

Explains:

1. the standalone executable model
2. bundled BASL source imports
3. why cross-compilation is out of scope

## Subcommand `--help`

Every major subcommand now accepts `--help` (or `-h`):

```sh
basl fmt --help
basl package --help
basl editor --help
```

This prints the same detailed page as:

```sh
basl help fmt
basl help package
```

This is useful when you already know the command and want to stay in that command’s context.

## Global Options

`basl --help` also documents the shared top-level options:

1. `--help`, `-h`
2. `--version`
3. `--tokens`
4. `--ast`
5. `--path <dir>`

These apply to normal script execution, not every subcommand.

For example:

```sh
basl --tokens app.basl
basl --ast app.basl
basl --path ./lib app.basl
```

## Recommended Usage Pattern

If you are learning the CLI:

```sh
basl --help
```

If you know the command but need details:

```sh
basl help <command>
```

If you are already typing the command:

```sh
basl <command> --help
```

If you need conceptual guidance:

```sh
basl help <topic>
```

## Examples

See what `doc` does:

```sh
basl help doc
```

See how tests are discovered:

```sh
basl help test
```

See how editor integrations are installed:

```sh
basl help editor
```

See how imports work before debugging module resolution:

```sh
basl help imports
```

See how standalone packaging works:

```sh
basl help packaging
basl help package
```

## Notes

1. Help text is built into the binary.
2. It does not depend on external markdown files at runtime.
3. Packaged BASL executables still run their bundled app first; command help applies to the normal `basl` CLI binary.
