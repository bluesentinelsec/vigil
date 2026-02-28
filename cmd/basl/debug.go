package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/interp"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

func runDebug(args []string) int {
	var scriptPath string
	var breakLines []int
	var searchPaths []string

	i := 0
	for i < len(args) {
		switch args[i] {
		case "-b", "--break":
			i++
			if i >= len(args) {
				fmt.Fprintln(os.Stderr, "error: -b requires a line number")
				return 2
			}
			n, err := strconv.Atoi(args[i])
			if err != nil || n < 1 {
				fmt.Fprintf(os.Stderr, "error: invalid line number %q\n", args[i])
				return 2
			}
			breakLines = append(breakLines, n)
		case "--path":
			i++
			if i >= len(args) {
				fmt.Fprintln(os.Stderr, "error: --path requires an argument")
				return 2
			}
			searchPaths = append(searchPaths, args[i])
		default:
			if scriptPath == "" {
				scriptPath = args[i]
			}
		}
		i++
	}

	if scriptPath == "" {
		fmt.Fprintln(os.Stderr, "usage: basl debug [-b <line>]... <file.basl>")
		return 2
	}

	src, err := os.ReadFile(scriptPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}

	lex := lexer.New(string(src))
	tokens, err := lex.Tokenize()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[lexer]: %s\n", err)
		return 1
	}

	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[parser]: %s\n", err)
		return 1
	}

	vm := interp.New()

	resolvedSearchPaths, err := resolveScriptSearchPaths(scriptPath, searchPaths)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}
	for _, sp := range resolvedSearchPaths {
		vm.AddSearchPath(sp)
	}

	displayName := filepath.Base(scriptPath)
	dbg := interp.NewDebugger(vm, displayName, string(src))
	for _, line := range breakLines {
		dbg.SetBreakpoint(line)
	}
	vm.SetDebugger(dbg)

	// If no breakpoints set, step from the start
	if len(breakLines) == 0 {
		dbg.StepFromStart()
	}

	fmt.Printf("BASL debugger — %s\n", displayName)
	if len(breakLines) > 0 {
		bps := make([]string, len(breakLines))
		for i, l := range breakLines {
			bps[i] = fmt.Sprintf("%d", l)
		}
		fmt.Printf("breakpoints: line %s\n", strings.Join(bps, ", "))
	} else {
		fmt.Println("no breakpoints set, stepping from start")
	}
	fmt.Println("type h for help")

	code, err := vm.Exec(prog)
	if err != nil {
		msg := err.Error()
		if msg == "debugger: quit" {
			fmt.Println("debugger: session ended")
			return 0
		}
		fmt.Fprintf(os.Stderr, "error[runtime]: %s\n", msg)
		return 1
	}
	fmt.Println("program exited normally")
	return code
}
