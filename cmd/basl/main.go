package main

import (
	"bufio"
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/bluesentinelsec/basl/pkg/basl"
	"github.com/bluesentinelsec/basl/pkg/basl/formatter"
	"github.com/bluesentinelsec/basl/pkg/basl/interp"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

const version = basl.Version

func init() {
	// Lock the main goroutine to the OS main thread.
	// Required for macOS GUI frameworks (Cocoa/SDL/etc.)
	runtime.LockOSThread()
}

func main() {
	args := os.Args[1:]
	if handled, code := tryRunPackagedBinary(args); handled {
		os.Exit(code)
		return
	}
	if len(args) > 0 {
		switch args[0] {
		case "--help", "-h":
			fmt.Print(mainHelpText())
			return
		case "--version":
			fmt.Println("basl " + basl.Version)
			return
		case "help":
			os.Exit(runHelp(args[1:]))
			return
		}
	}
	if len(args) == 0 {
		runRepl()
		return
	}

	// Subcommands
	if args[0] == "fmt" {
		os.Exit(runFmt(args[1:]))
		return
	}
	if args[0] == "check" {
		os.Exit(runCheck(args[1:]))
		return
	}
	if args[0] == "package" {
		os.Exit(runPackage(args[1:]))
		return
	}
	if args[0] == "editor" {
		os.Exit(runEditor(args[1:]))
		return
	}
	if args[0] == "lsp" {
		os.Exit(runLSP(args[1:]))
		return
	}
	if args[0] == "dap" {
		os.Exit(runDAP(args[1:]))
		return
	}
	if args[0] == "doc" {
		os.Exit(runDoc(args[1:]))
		return
	}
	if args[0] == "test" {
		os.Exit(runTest(args[1:]))
		return
	}
	if args[0] == "embed" {
		os.Exit(runEmbed(args[1:]))
		return
	}
	if args[0] == "new" {
		os.Exit(runNew(args[1:]))
		return
	}
	if args[0] == "debug" {
		os.Exit(runDebug(args[1:]))
		return
	}
	if args[0] == "get" {
		os.Exit(runGet(args[1:]))
		return
	}
	if args[0] == "remove" {
		os.Exit(runRemove(args[1:]))
		return
	}
	if args[0] == "upgrade" {
		os.Exit(runUpgrade(args[1:]))
		return
	}
	if args[0] == "deps" {
		os.Exit(runDeps(args[1:]))
		return
	}

	scriptPath := ""
	showTokens := false
	showAST := false
	var scriptArgs []string
	var searchPaths []string

	i := 0
	for i < len(args) {
		if args[i] == "--" {
			i++
			// Everything after -- is script args
			for i < len(args) {
				if scriptPath == "" {
					scriptPath = args[i]
				} else {
					scriptArgs = append(scriptArgs, args[i])
				}
				i++
			}
			break
		}
		switch args[i] {
		case "--tokens":
			showTokens = true
		case "--ast":
			showAST = true
		case "--path":
			i++
			if i >= len(args) {
				fmt.Fprintln(os.Stderr, "error: --path requires an argument")
				os.Exit(2)
			}
			searchPaths = append(searchPaths, args[i])
		default:
			if scriptPath == "" {
				scriptPath = args[i]
			} else {
				scriptArgs = append(scriptArgs, args[i])
			}
		}
		i++
	}

	if scriptPath == "" {
		fmt.Fprintln(os.Stderr, "error: no script file specified")
		os.Exit(2)
	}

	if !strings.HasSuffix(scriptPath, ".basl") {
		if !strings.Contains(scriptPath, string(filepath.Separator)) && !strings.Contains(scriptPath, ".") {
			fmt.Fprintf(os.Stderr, "error: unknown command %q. Run 'basl --help' for usage.\n", scriptPath)
		} else {
			fmt.Fprintf(os.Stderr, "error: scripts must have a .basl extension: %s\n", scriptPath)
		}
		os.Exit(1)
	}

	src, err := os.ReadFile(scriptPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		os.Exit(1)
	}

	lex := lexer.New(string(src))
	tokens, err := lex.Tokenize()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[lexer]: %s\n", err)
		os.Exit(1)
	}

	if showTokens {
		for _, t := range tokens {
			fmt.Println(t)
		}
		return
	}

	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[parser]: %s\n", err)
		os.Exit(1)
	}

	if showAST {
		fmt.Printf("Program with %d declarations\n", len(prog.Decls))
		return
	}

	vm := interp.New()
	vm.RegisterScriptArgs(scriptArgs)

	resolvedSearchPaths, err := resolveScriptSearchPaths(scriptPath, searchPaths)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		os.Exit(1)
	}
	for _, sp := range resolvedSearchPaths {
		vm.AddSearchPath(sp)
	}

	code, err := vm.Exec(prog)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[runtime]: %s\n", err)
		os.Exit(1)
	}
	os.Exit(code)
}

func runFmt(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("fmt"))
		return 0
	}

	check := false
	var targets []string
	for _, a := range args {
		if a == "--check" {
			check = true
		} else {
			targets = append(targets, a)
		}
	}
	if len(targets) == 0 {
		fmt.Fprintln(os.Stderr, "usage: basl fmt [--check] <file.basl|./dir/...>")
		return 2
	}

	files, err := collectFiles(targets)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}

	exitCode := 0
	for _, path := range files {
		changed, err := fmtFile(path, check)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error[fmt]: %s: %s\n", path, err)
			exitCode = 1
			continue
		}
		if check && changed {
			fmt.Fprintln(os.Stderr, path)
			exitCode = 1
		}
	}
	return exitCode
}

func fmtFile(path string, check bool) (bool, error) {
	src, err := os.ReadFile(path)
	if err != nil {
		return false, err
	}

	// Lex with comments
	lex := lexer.New(string(src))
	tokensWithComments, err := lex.TokenizeWithComments()
	if err != nil {
		return false, err
	}

	// Lex without comments for parser
	lex2 := lexer.New(string(src))
	tokens, err := lex2.Tokenize()
	if err != nil {
		return false, err
	}

	// Parse
	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		return false, err
	}

	// Format
	out := formatter.Format(prog, tokensWithComments)

	if bytes.Equal(src, out) {
		return false, nil
	}

	if !check {
		if err := os.WriteFile(path, out, 0644); err != nil {
			return false, err
		}
	}
	return true, nil
}

func collectFiles(targets []string) ([]string, error) {
	var files []string
	for _, t := range targets {
		if strings.HasSuffix(t, "/...") {
			dir := strings.TrimSuffix(t, "/...")
			if dir == "." || dir == "" {
				dir = "."
			}
			err := filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
				if err != nil {
					return err
				}
				if !info.IsDir() && strings.HasSuffix(path, ".basl") {
					files = append(files, path)
				}
				return nil
			})
			if err != nil {
				return nil, err
			}
		} else {
			files = append(files, t)
		}
	}
	return files, nil
}

func runRepl() {
	fmt.Printf("basl %s — type expressions, statements, or declarations. Ctrl-D to exit.\n", version)
	vm := interp.New()
	cwd, _ := os.Getwd()
	vm.AddSearchPath(cwd)

	scanner := bufio.NewScanner(os.Stdin)
	var buf strings.Builder
	depth := 0

	for {
		if depth == 0 {
			fmt.Print(">>> ")
		} else {
			fmt.Print("... ")
		}
		if !scanner.Scan() {
			fmt.Println()
			break
		}
		line := scanner.Text()

		if depth == 0 && strings.TrimSpace(line) == "" {
			continue
		}

		buf.WriteString(line)
		buf.WriteString("\n")
		depth += strings.Count(line, "{") - strings.Count(line, "}")

		if depth > 0 {
			continue
		}
		depth = 0

		src := strings.TrimSpace(buf.String())
		buf.Reset()

		result, err := vm.ReplEval(src)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			continue
		}
		if result != "" {
			fmt.Println(result)
		}
	}
}

func runTest(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("test"))
		return 0
	}

	verbose := false
	filter := ""
	var targets []string

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "-v", "--verbose":
			verbose = true
		case "-run", "--run":
			i++
			if i >= len(args) {
				fmt.Fprintln(os.Stderr, "error: -run requires a pattern")
				return 2
			}
			filter = args[i]
		default:
			targets = append(targets, args[i])
		}
	}

	// Default: current directory recursive
	if len(targets) == 0 {
		var err error
		targets, err = defaultTestTargets()
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
	}

	// Collect all _test.basl files
	var testFiles []string
	for _, t := range targets {
		info, err := os.Stat(t)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		if info.IsDir() {
			filepath.Walk(t, func(path string, fi os.FileInfo, err error) error {
				if err != nil {
					return err
				}
				if !fi.IsDir() && strings.HasSuffix(path, "_test.basl") {
					testFiles = append(testFiles, path)
				}
				return nil
			})
		} else if strings.HasSuffix(t, "_test.basl") {
			testFiles = append(testFiles, t)
		}
	}

	if len(testFiles) == 0 {
		fmt.Println("no test files found")
		return 0
	}

	totalPass := 0
	totalFail := 0
	exitCode := 0

	for _, tf := range testFiles {
		src, err := os.ReadFile(tf)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			exitCode = 1
			continue
		}

		fileStart := time.Now()
		searchPaths, err := resolveTestSearchPaths(tf)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error[%s]: %s\n", tf, err)
			exitCode = 1
			continue
		}
		results, err := interp.ExecTestFile(tf, src, filter, searchPaths)
		fileElapsed := time.Since(fileStart)

		if err != nil {
			fmt.Fprintf(os.Stderr, "error[%s]: %s\n", tf, err)
			exitCode = 1
			continue
		}

		fileFailed := false
		for _, r := range results {
			if r.Passed {
				totalPass++
				if verbose {
					fmt.Printf("=== RUN   %s\n--- PASS: %s (%.3fs)\n", r.Name, r.Name, r.Elapsed.Seconds())
				}
			} else {
				totalFail++
				fileFailed = true
				fmt.Printf("--- FAIL: %s (%s)\n    %s\n", r.Name, tf, r.Message)
			}
		}

		if fileFailed {
			fmt.Printf("FAIL\t%s\t%.3fs\n", tf, fileElapsed.Seconds())
			exitCode = 1
		} else if len(results) > 0 {
			fmt.Printf("ok  \t%s\t%.3fs\n", tf, fileElapsed.Seconds())
		}
	}

	if totalFail > 0 {
		fmt.Printf("\nFAIL: %d passed, %d failed\n", totalPass, totalFail)
	} else if totalPass > 0 {
		fmt.Printf("\nPASS: %d passed\n", totalPass)
	}

	return exitCode
}
