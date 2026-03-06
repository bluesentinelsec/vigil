package main

import (
	"strings"
	"testing"
)

func TestMainHelpTextIncludesCommandsAndTopics(t *testing.T) {
	out := mainHelpText()

	for _, want := range []string{
		"BASL",
		"USAGE",
		"COMMANDS",
		"  fmt      Format BASL source files in place or check formatting",
		"  check    Statically validate BASL source files without executing them",
		"  package  Package a BASL project as an executable or library",
		"  editor   Manage bundled editor integrations and semantic editor queries",
		"  lsp      Run the BASL language server over stdio",
		"TOPICS",
		"  imports    How BASL resolves imported modules",
		"  basl help package",
	} {
		if !strings.Contains(out, want) {
			t.Fatalf("mainHelpText() missing %q:\n%s", want, out)
		}
	}

	checkOut, ok := helpTextFor("check")
	if !ok {
		t.Fatal("helpTextFor(check) returned not found")
	}
	for _, want := range []string{
		"COMMAND\n  check",
		"basl check [--path dir] [file.basl|dir|./dir/...]",
		"Parses BASL files and reports static diagnostics without running user code.",
	} {
		if !strings.Contains(checkOut, want) {
			t.Fatalf("check help missing %q:\n%s", want, checkOut)
		}
	}

	editorOut, ok := helpTextFor("editor")
	if !ok {
		t.Fatal("helpTextFor(editor) returned not found")
	}
	for _, want := range []string{
		"COMMAND\n  editor",
		"basl editor list",
		"basl editor install [--home dir] [--force] <editor...>",
		"basl editor semantic <operation> --file path [options]",
		"Use `basl editor list` to see the supported editor targets.",
	} {
		if !strings.Contains(editorOut, want) {
			t.Fatalf("editor help missing %q:\n%s", want, editorOut)
		}
	}

	lspOut, ok := helpTextFor("lsp")
	if !ok {
		t.Fatal("helpTextFor(lsp) returned not found")
	}
	for _, want := range []string{
		"COMMAND\n  lsp",
		"basl lsp",
		"Starts the BASL Language Server Protocol (LSP) server over stdin/stdout.",
	} {
		if !strings.Contains(lspOut, want) {
			t.Fatalf("lsp help missing %q:\n%s", want, lspOut)
		}
	}
}

func TestHelpTextForPackageAndTopic(t *testing.T) {
	out, ok := helpTextFor("package")
	if !ok {
		t.Fatal("helpTextFor(package) returned not found")
	}
	for _, want := range []string{
		"COMMAND\n  package",
		"basl package --inspect <binary>",
		"Auto-detects project type and packages accordingly.",
	} {
		if !strings.Contains(out, want) {
			t.Fatalf("package help missing %q:\n%s", want, out)
		}
	}

	topicOut, ok := helpTextFor("imports")
	if !ok {
		t.Fatal("helpTextFor(imports) returned not found")
	}
	for _, want := range []string{
		"TOPIC\n  imports",
		"Builtin modules like fmt, os, json, file, and thread are resolved from the interpreter.",
		"basl --path ./vendor app.basl",
	} {
		if !strings.Contains(topicOut, want) {
			t.Fatalf("imports help missing %q:\n%s", want, topicOut)
		}
	}
}
