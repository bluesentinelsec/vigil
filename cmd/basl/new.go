package main

import (
	"fmt"
	"os"
	"path/filepath"
)

func runNew(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("new"))
		return 0
	}

	lib := false
	name := ""

	for _, a := range args {
		switch a {
		case "--lib":
			lib = true
		default:
			if name == "" {
				name = a
			} else {
				fmt.Fprintf(os.Stderr, "error: unexpected argument %q\n", a)
				return 2
			}
		}
	}

	if name == "" {
		fmt.Fprintln(os.Stderr, "usage: basl new <project-name> [--lib]")
		return 2
	}

	root, _ := filepath.Abs(name)

	// Check if directory already exists and is non-empty
	if entries, err := os.ReadDir(root); err == nil && len(entries) > 0 {
		fmt.Fprintf(os.Stderr, "error: directory %s already exists and is not empty\n", name)
		return 1
	}

	dirs := []string{
		root,
		filepath.Join(root, "lib"),
		filepath.Join(root, "test"),
	}
	for _, d := range dirs {
		if err := os.MkdirAll(d, 0755); err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
	}

	baseName := filepath.Base(name)

	// basl.toml
	toml := fmt.Sprintf("name = %q\nversion = \"0.1.0\"\n", baseName)
	writeFile(root, "basl.toml", toml)

	// .gitignore
	writeFile(root, ".gitignore", "deps/\n")

	if lib {
		// Library project
		writeFile(filepath.Join(root, "lib"), baseName+".basl",
			fmt.Sprintf("/// %s library module.\n\npub fn hello() -> string {\n    return \"hello from %s\";\n}\n", baseName, baseName))
		writeFile(filepath.Join(root, "test"), baseName+"_test.basl",
			fmt.Sprintf("import \"t\";\nimport \"%s\";\n\nfn test_hello() -> void {\n    t.assert(%s.hello() == \"hello from %s\", \"hello should match\");\n}\n", baseName, baseName, baseName))
	} else {
		// Application project
		writeFile(root, "main.basl",
			"import \"fmt\";\n\nfn main() -> i32 {\n    fmt.println(\"hello, world!\");\n    return 0;\n}\n")
	}

	fmt.Printf("created %s/\n", name)
	if lib {
		fmt.Printf("  basl.toml\n  lib/%s.basl\n  test/%s_test.basl\n  .gitignore\n", baseName, baseName)
	} else {
		fmt.Printf("  basl.toml\n  main.basl\n  lib/\n  test/\n  .gitignore\n")
	}
	return 0
}

func writeFile(dir, name, content string) {
	path := filepath.Join(dir, name)
	os.WriteFile(path, []byte(content), 0644)
}
