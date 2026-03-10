# BASL Editor Integration CLI

The `basl editor` command manages the editor integrations bundled into the `basl` binary and exposes the semantic JSON queries used for debugging the language tooling.

Installation is a local convenience feature:

1. it installs editor support from files already embedded in `basl`
2. it does not download anything
3. it only writes to the editor-specific paths you explicitly choose

The semantic subcommand is read-only:

1. it parses BASL files and prints JSON to stdout
2. it does not modify source files
3. it is useful for debugging editor features without speaking the full LSP protocol

## Supported Editors

List the currently supported editor targets:

```sh
basl editor list
```

Current targets:

1. `vim`
2. `nvim`
3. `vscode`

## Usage

Show available editor targets:

```sh
basl editor list
```

Install one or more editor targets:

```sh
basl editor install [--home dir] [--force] <editor...>
```

Remove one or more editor targets:

```sh
basl editor uninstall [--home dir] <editor...>
```

Run a semantic query:

```sh
basl editor semantic <diagnostics|definition|hover|references|rename|completions> --file path [options]
```

Run the real language server:

```sh
basl lsp
```

Examples:

```sh
basl editor list
basl editor install vim
basl editor install nvim vscode
basl editor install --home /tmp/basl-home vim
basl editor install --force vscode
basl editor uninstall vim vscode
basl editor semantic diagnostics --file main.basl
basl editor semantic definition --file main.basl --line 12 --col 9
basl editor semantic rename --file main.basl --line 12 --col 9 --to newName
```

## Install Paths

### Vim

`basl editor install vim` writes:

1. `~/.vim/ftdetect/basl.vim`
2. `~/.vim/syntax/basl.vim`

This enables:

1. automatic `*.basl` filetype detection
2. BASL syntax highlighting

### Neovim

`basl editor install nvim` writes:

1. `~/.config/nvim/ftdetect/basl.vim`
2. `~/.config/nvim/syntax/basl.vim`

This uses the same BASL Vim syntax files, installed in the Neovim runtime location.

### VS Code

`basl editor install vscode` writes the bundled BASL extension into:

1. `~/.vscode/extensions/basl/`

That directory contains:

1. `package.json`
2. the BASL TextMate grammar
3. language configuration
4. snippets
5. completions metadata

You may need to reload VS Code after installation or removal.

## `--home`

Use `--home` to install into an alternate home directory:

```sh
basl editor install --home /tmp/basl-home vim
```

This changes where BASL writes the editor files.

It is useful for:

1. testing
2. custom dotfile staging
3. scripted setup in a non-default home directory

With `--home /tmp/basl-home`, the Vim target installs into:

1. `/tmp/basl-home/.vim/ftdetect/basl.vim`
2. `/tmp/basl-home/.vim/syntax/basl.vim`

## `--force`

By default, `basl editor install` is conservative:

1. if a managed BASL editor file does not exist, it is created
2. if it already exists with the same contents, it is left alone
3. if it exists with different contents, installation fails

Use `--force` to overwrite conflicting installed files:

```sh
basl editor install --force vim
```

This is mainly relevant for the Vim and Neovim targets, where a user may have manually edited the installed BASL syntax files.

## Uninstall Behavior

`basl editor uninstall` removes only the files managed by BASL for the selected target.

Examples:

```sh
basl editor uninstall vim
basl editor uninstall nvim vscode
```

Behavior by target:

1. `vim`: removes the BASL files under `~/.vim/`
2. `nvim`: removes the BASL files under `~/.config/nvim/`
3. `vscode`: removes the managed extension directory `~/.vscode/extensions/basl/`

The command does not remove unrelated editor configuration.

## Semantic Queries

`basl editor semantic` remains useful for manual debugging and testing of the semantic engine.

Current operations:

1. `diagnostics`: static diagnostics as JSON
2. `definition`: definition location at `--line/--col`
3. `hover`: symbol detail at `--line/--col`
4. `references`: reference locations at `--line/--col`
5. `rename`: rename edits at `--line/--col --to newName`
6. `completions`: completion items at `--line/--col`

Positions are 1-based.

Examples:

```sh
basl editor semantic diagnostics --file main.basl
basl editor semantic hover --file main.basl --line 8 --col 15
basl editor semantic completions --file main.basl --line 8 --col 22
```

## Language Server

The bundled VS Code extension now launches `basl lsp` over stdio for semantic editor features.

Current LSP-backed features:

1. diagnostics
2. go to definition
3. hover
4. find references
5. rename symbol
6. document symbols
7. completions

Important separation:

1. the existing Vim syntax files and VS Code grammar still control tokenization and syntax highlighting
2. the LSP adds semantic behavior on top of that highlighting layer
3. testing LSP support should focus on diagnostics, navigation, rename, symbols, and completions, not on colors alone

## Recommended Workflow

For a local machine:

```sh
basl editor list
basl editor install vim vscode
```

Then:

1. restart or reload the editor if needed
2. open a `.basl` file
3. confirm the editor recognizes BASL syntax

For testing the installer safely:

```sh
basl editor install --home /tmp/basl-home vim vscode
basl editor uninstall --home /tmp/basl-home vim vscode
```

## Troubleshooting

### `unknown editor "..."`

Cause:

1. the named target is not bundled into this `basl` binary

Fix:

1. run `basl editor list`
2. retry with one of the listed names

### `... already exists and differs; rerun with --force to overwrite`

Cause:

1. BASL found an existing file at the managed install path
2. the file does not match the embedded BASL editor asset

Fix:

1. review the existing file if you want to preserve local edits
2. rerun with `--force` if you want the bundled BASL file to replace it

### VS Code still does not highlight BASL

Cause:

1. VS Code has not reloaded the unpacked extension yet

Fix:

1. reload the VS Code window
2. restart VS Code
3. confirm the extension files exist under `~/.vscode/extensions/basl/`
