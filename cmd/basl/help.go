package main

import (
	"fmt"
	"os"
	"sort"
	"strings"
)

type helpEntry struct {
	Name        string
	Summary     string
	Usage       []string
	Description []string
	Examples    []string
}

var commandHelpOrder = []string{"new", "fmt", "doc", "test", "debug", "get", "remove", "upgrade", "deps", "package", "editor", "embed", "help"}
var topicHelpOrder = []string{"run", "imports", "packaging"}

var commandHelp = map[string]helpEntry{
	"new": {
		Name:    "new",
		Summary: "Create a new BASL project with standard directory layout",
		Usage: []string{
			"basl new <project-name>",
			"basl new <project-name> --lib",
		},
		Description: []string{
			"Scaffolds a new BASL project with the standard directory structure.",
			"Use --lib to create a library project (no main.basl).",
		},
		Examples: []string{
			"basl new myapp",
			"basl new mylib --lib",
		},
	},
	"fmt": {
		Name:    "fmt",
		Summary: "Format BASL source files in place or check formatting",
		Usage: []string{
			"basl fmt [--check] <file.basl|./dir/...>",
		},
		Description: []string{
			"Formats one or more BASL files using the built-in formatter.",
			"Use --check to verify formatting without rewriting files.",
		},
		Examples: []string{
			"basl fmt script.basl",
			"basl fmt ./examples/...",
			"basl fmt --check ./examples/...",
		},
	},
	"doc": {
		Name:    "doc",
		Summary: "Show public API documentation for a BASL source file",
		Usage: []string{
			"basl doc <file.basl>",
			"basl doc <file.basl> <symbol>",
		},
		Description: []string{
			"Parses a BASL file and renders documentation for public symbols.",
			"Use a symbol name like Point or Point.distance to focus the output.",
		},
		Examples: []string{
			"basl doc ./examples/doc_demo.basl",
			"basl doc ./examples/doc_demo.basl Job.start",
		},
	},
	"test": {
		Name:    "test",
		Summary: "Run BASL test files ending in _test.basl",
		Usage: []string{
			"basl test [--run pattern] [path...]",
		},
		Description: []string{
			"Recursively finds and runs BASL test files.",
			"In a BASL project root, `basl test` defaults to the ./test directory and automatically resolves imports from lib/ and deps/.",
			"Use -run to filter test names and -v for verbose output.",
		},
		Examples: []string{
			"basl test",
			"basl test ./stdlib/...",
			"basl test -run parse -v ./parser/...",
		},
	},
	"debug": {
		Name:    "debug",
		Summary: "Run a BASL program with interactive debugger",
		Usage: []string{
			"basl debug [-b <line>]... <file.basl>",
		},
		Description: []string{
			"Launches the interactive debugger. Set breakpoints with -b or",
			"step from the start if none are set. Supports stepping, variable",
			"inspection, and backtrace.",
		},
		Examples: []string{
			"basl debug main.basl",
			"basl debug -b 10 main.basl",
			"basl debug -b 5 -b 12 main.basl",
		},
	},
	"get": {
		Name:    "get",
		Summary: "Add a dependency from a git repository",
		Usage: []string{
			"basl get <git-url>[@<tag|commit>]",
		},
		Description: []string{
			"Clones a git repository into deps/ and adds it to basl.toml.",
			"Use @tag or @commit to pin a specific version.",
		},
		Examples: []string{
			"basl get https://github.com/user/json_schema@v1.2.0",
			"basl get https://github.com/user/utils",
		},
	},
	"remove": {
		Name:    "remove",
		Summary: "Remove a dependency",
		Usage: []string{
			"basl remove <package-name>",
		},
		Description: []string{
			"Removes a dependency from basl.toml and deletes it from deps/.",
		},
		Examples: []string{
			"basl remove json_schema",
		},
	},
	"upgrade": {
		Name:    "upgrade",
		Summary: "Upgrade dependencies to newer versions",
		Usage: []string{
			"basl upgrade [<name>[@<tag>]]",
		},
		Description: []string{
			"Upgrades one or all dependencies. Fetches latest tags from remote.",
			"Use @tag to upgrade to a specific version.",
		},
		Examples: []string{
			"basl upgrade",
			"basl upgrade json_schema",
			"basl upgrade json_schema@v2.0.0",
		},
	},
	"deps": {
		Name:    "deps",
		Summary: "Sync dependencies to match basl.toml",
		Usage: []string{
			"basl deps",
		},
		Description: []string{
			"Clones or updates all dependencies listed in basl.toml.",
			"Run this after cloning a project to fetch its dependencies.",
		},
		Examples: []string{
			"basl deps",
		},
	},
	"package": {
		Name:    "package",
		Summary: "Build or inspect standalone BASL executables",
		Usage: []string{
			"basl package [-o output] [--path dir] [<entry.basl|project-dir>]",
			"basl package --inspect <binary>",
		},
		Description: []string{
			"Creates a standalone executable by copying the current basl binary and appending a bundled BASL payload.",
			"In a BASL project root, `basl package` defaults to ./main.basl and resolves imports from lib/ and deps/ automatically.",
			"Use --inspect to show the packaged entrypoint and bundled BASL files.",
		},
		Examples: []string{
			"basl package",
			"basl package ./myapp",
			"basl package -o myapp ./app/main.basl",
			"basl package --path ./lib ./app/main.basl",
			"basl package --inspect ./myapp",
		},
	},
	"editor": {
		Name:    "editor",
		Summary: "List, install, or remove bundled editor integrations",
		Usage: []string{
			"basl editor list",
			"basl editor install [--home dir] [--force] <editor...>",
			"basl editor uninstall [--home dir] <editor...>",
		},
		Description: []string{
			"Installs or removes the editor files bundled into the basl binary.",
			"Use `basl editor list` to see the supported editor targets.",
			"`--home` installs into an alternate home directory, which is useful for testing or custom setups.",
			"`--force` overwrites existing editor files when installing.",
		},
		Examples: []string{
			"basl editor list",
			"basl editor install vim",
			"basl editor install vscode --force",
			"basl editor uninstall vim vscode",
		},
	},
	"embed": {
		Name:    "embed",
		Summary: "Generate BASL modules that embed external file contents",
		Usage: []string{
			"basl embed <file|dir...> [-o output.basl] [--compress|--no-compress]",
		},
		Description: []string{
			"Generates a BASL source module containing file data encoded as base64.",
			"When embedding multiple files or a directory, the output is a BASL asset module.",
		},
		Examples: []string{
			"basl embed logo.png -o logo_asset.basl",
			"basl embed ./assets -o assets.basl",
		},
	},
	"help": {
		Name:    "help",
		Summary: "Show general help or help for a command or topic",
		Usage: []string{
			"basl help",
			"basl help <command|topic>",
		},
		Description: []string{
			"Shows the command overview by default.",
			"Use a command name like package or a topic like imports for detailed help.",
		},
		Examples: []string{
			"basl help package",
			"basl help imports",
		},
	},
}

var topicHelp = map[string]helpEntry{
	"run": {
		Name:    "run",
		Summary: "Run BASL scripts directly from the CLI",
		Usage: []string{
			"basl <script.basl> [args...]",
			"basl [--tokens] [--ast] [--path dir] <script.basl> [args...]",
		},
		Description: []string{
			"The first positional BASL file is treated as the script entrypoint.",
			"Arguments after the script path are forwarded to os.args() inside the script.",
			"When the script is inside a BASL project, imports automatically resolve from the project lib/ and deps/ directories.",
			"Use -- to stop CLI option parsing and force the remaining values to be script arguments.",
		},
		Examples: []string{
			"basl app.basl",
			"basl app.basl alpha beta",
			"basl --tokens app.basl",
			"basl --path ./lib app.basl -- --literal-arg",
		},
	},
	"imports": {
		Name:    "imports",
		Summary: "How BASL resolves imported modules",
		Usage: []string{
			"import \"fmt\";",
			"import \"shared/math\";",
			"basl --path ./lib app.basl",
		},
		Description: []string{
			"Builtin modules like fmt, os, json, file, and thread are resolved from the interpreter.",
			"In a BASL project, file-backed modules resolve from lib/ first, then deps/.",
			"Outside a project, file-backed BASL modules are resolved from the script directory first, then each --path directory.",
			"Only pub symbols are visible when importing BASL source modules.",
		},
		Examples: []string{
			"import \"fmt\";",
			"import \"utils\";",
			"import \"http/router\";",
			"basl --path ./vendor app.basl",
		},
	},
	"packaging": {
		Name:    "packaging",
		Summary: "How standalone BASL executables are built",
		Usage: []string{
			"basl package -o myapp ./app/main.basl",
			"basl package --inspect ./myapp",
		},
		Description: []string{
			"The packaged output is a copy of the current basl executable plus an appended BASL payload.",
			"The payload contains the entry script and reachable BASL source imports, rewritten to internal bundle paths.",
			"In a BASL project root, packaging defaults to main.basl and searches lib/ and deps/ automatically.",
			"Cross-compilation is intentionally unsupported.",
		},
		Examples: []string{
			"basl package",
			"basl package ./examples/app.basl",
			"basl package --inspect ./app",
		},
	},
}

func runHelp(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("help"))
		return 0
	}
	if len(args) == 0 {
		fmt.Print(mainHelpText())
		return 0
	}
	if len(args) > 1 {
		fmt.Fprintln(os.Stderr, "usage: basl help [command|topic]")
		return 2
	}

	out, ok := helpTextFor(args[0])
	if !ok {
		fmt.Fprintf(os.Stderr, "error: unknown help topic %q\n", args[0])
		fmt.Fprintln(os.Stderr, "run `basl --help` to see available commands and topics")
		return 2
	}
	fmt.Print(out)
	return 0
}

func wantsHelp(args []string) bool {
	for _, arg := range args {
		if arg == "--help" || arg == "-h" {
			return true
		}
	}
	return false
}

func mainHelpText() string {
	var b strings.Builder
	b.WriteString("BASL\n")
	b.WriteString("  Typed scripting language interpreter and CLI toolchain.\n\n")

	b.WriteString("USAGE\n")
	b.WriteString("  basl <script.basl> [args...]\n")
	b.WriteString("  basl <command> [options]\n\n")

	b.WriteString("COMMANDS\n")
	for _, name := range commandHelpOrder {
		entry := commandHelp[name]
		fmt.Fprintf(&b, "  %-8s %s\n", entry.Name, entry.Summary)
	}
	b.WriteString("\n")

	b.WriteString("GLOBAL OPTIONS\n")
	b.WriteString("  --help, -h        Show general help\n")
	b.WriteString("  --version         Print the basl version\n")
	b.WriteString("  --tokens          Print lexer tokens for a script\n")
	b.WriteString("  --ast             Print a basic AST summary for a script\n")
	b.WriteString("  --path <dir>      Add a module search path for script execution\n\n")

	b.WriteString("TOPICS\n")
	for _, name := range topicHelpOrder {
		entry := topicHelp[name]
		fmt.Fprintf(&b, "  %-10s %s\n", entry.Name, entry.Summary)
	}
	b.WriteString("\n")

	b.WriteString("EXAMPLES\n")
	b.WriteString("  basl new myapp\n")
	b.WriteString("  basl app.basl\n")
	b.WriteString("  basl fmt ./examples/...\n")
	b.WriteString("  basl doc ./examples/doc_demo.basl Job.start\n")
	b.WriteString("  basl test ./...\n")
	b.WriteString("  basl package\n")
	b.WriteString("  basl editor list\n")
	b.WriteString("  basl help package\n")

	return b.String()
}

func helpTextFor(name string) (string, bool) {
	if entry, ok := commandHelp[name]; ok {
		return renderHelpEntry(entry, "COMMAND"), true
	}
	if entry, ok := topicHelp[name]; ok {
		return renderHelpEntry(entry, "TOPIC"), true
	}
	return "", false
}

func mustHelpText(name string) string {
	out, ok := helpTextFor(name)
	if !ok {
		panic("missing help entry for " + name)
	}
	return out
}

func renderHelpEntry(entry helpEntry, kind string) string {
	var b strings.Builder
	fmt.Fprintf(&b, "%s\n  %s\n\n", kind, entry.Name)
	fmt.Fprintf(&b, "SUMMARY\n  %s\n\n", entry.Summary)

	b.WriteString("USAGE\n")
	for _, line := range entry.Usage {
		b.WriteString("  ")
		b.WriteString(line)
		b.WriteString("\n")
	}

	if len(entry.Description) > 0 {
		b.WriteString("\nDETAILS\n")
		for _, line := range entry.Description {
			b.WriteString("  ")
			b.WriteString(line)
			b.WriteString("\n")
		}
	}

	if len(entry.Examples) > 0 {
		b.WriteString("\nEXAMPLES\n")
		for _, line := range entry.Examples {
			b.WriteString("  ")
			b.WriteString(line)
			b.WriteString("\n")
		}
	}

	var also []string
	for _, name := range commandHelpOrder {
		if name != entry.Name {
			also = append(also, name)
		}
	}
	for _, name := range topicHelpOrder {
		if name != entry.Name {
			also = append(also, name)
		}
	}
	sort.Strings(also)

	if len(also) > 0 {
		b.WriteString("\nSEE ALSO\n")
		for _, name := range also {
			b.WriteString("  basl help ")
			b.WriteString(name)
			b.WriteString("\n")
		}
	}

	return b.String()
}
