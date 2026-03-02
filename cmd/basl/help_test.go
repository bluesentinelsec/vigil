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
		"  package  Package a BASL project as an executable or library",
		"  editor   List, install, or remove bundled editor integrations",
		"TOPICS",
		"  imports    How BASL resolves imported modules",
		"  basl help package",
	} {
		if !strings.Contains(out, want) {
			t.Fatalf("mainHelpText() missing %q:\n%s", want, out)
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
		"Use `basl editor list` to see the supported editor targets.",
	} {
		if !strings.Contains(editorOut, want) {
			t.Fatalf("editor help missing %q:\n%s", want, editorOut)
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
