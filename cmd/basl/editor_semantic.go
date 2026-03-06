package main

import (
	"encoding/json"
	"fmt"
	"os"
	"strconv"

	basleditor "github.com/bluesentinelsec/basl/pkg/basl/editor"
)

type editorSemanticArgs struct {
	file        string
	line        int
	col         int
	renameTo    string
	searchPaths []string
}

func runEditorSemantic(args []string) int {
	if len(args) == 0 || wantsHelp(args) {
		fmt.Fprintln(os.Stderr, "usage: basl editor semantic <diagnostics|definition|hover|references|rename|completions> --file path [options]")
		return 2
	}

	op := args[0]
	parsed, err := parseEditorSemanticArgs(args[1:])
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 2
	}
	if parsed.file == "" {
		fmt.Fprintln(os.Stderr, "error: --file is required")
		return 2
	}

	switch op {
	case "diagnostics":
		diags, err := basleditor.Diagnostics(parsed.file, parsed.searchPaths)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		return writeJSON(diags)
	case "definition":
		loc, err := basleditor.Definition(parsed.file, basleditor.Position{Line: parsed.line, Col: parsed.col}, parsed.searchPaths)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		return writeJSON(loc)
	case "hover":
		hover, err := basleditor.HoverAt(parsed.file, basleditor.Position{Line: parsed.line, Col: parsed.col}, parsed.searchPaths)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		return writeJSON(hover)
	case "references":
		refs, err := basleditor.References(parsed.file, basleditor.Position{Line: parsed.line, Col: parsed.col}, parsed.searchPaths)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		return writeJSON(refs)
	case "rename":
		if parsed.renameTo == "" {
			fmt.Fprintln(os.Stderr, "error: rename requires --to <new-name>")
			return 2
		}
		edits, err := basleditor.Rename(parsed.file, basleditor.Position{Line: parsed.line, Col: parsed.col}, parsed.renameTo, parsed.searchPaths)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		return writeJSON(edits)
	case "completions":
		items, err := basleditor.Completions(parsed.file, basleditor.Position{Line: parsed.line, Col: parsed.col}, parsed.searchPaths)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		return writeJSON(items)
	default:
		fmt.Fprintf(os.Stderr, "error: unknown semantic operation %q\n", op)
		return 2
	}
}

func parseEditorSemanticArgs(args []string) (editorSemanticArgs, error) {
	var out editorSemanticArgs
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--file":
			i++
			if i >= len(args) {
				return out, fmt.Errorf("--file requires a path")
			}
			out.file = args[i]
		case "--line":
			i++
			if i >= len(args) {
				return out, fmt.Errorf("--line requires a value")
			}
			n, err := strconv.Atoi(args[i])
			if err != nil || n <= 0 {
				return out, fmt.Errorf("--line must be a positive integer")
			}
			out.line = n
		case "--col":
			i++
			if i >= len(args) {
				return out, fmt.Errorf("--col requires a value")
			}
			n, err := strconv.Atoi(args[i])
			if err != nil || n <= 0 {
				return out, fmt.Errorf("--col must be a positive integer")
			}
			out.col = n
		case "--to":
			i++
			if i >= len(args) {
				return out, fmt.Errorf("--to requires a value")
			}
			out.renameTo = args[i]
		case "--path":
			i++
			if i >= len(args) {
				return out, fmt.Errorf("--path requires a directory")
			}
			out.searchPaths = append(out.searchPaths, args[i])
		default:
			return out, fmt.Errorf("unknown option %q", args[i])
		}
	}
	return out, nil
}

func writeJSON(v any) int {
	data, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}
	if _, err := os.Stdout.Write(append(data, '\n')); err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}
	return 0
}
