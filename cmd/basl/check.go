package main

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/checker"
)

func runCheck(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("check"))
		return 0
	}

	var targets []string
	var searchPaths []string
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--path":
			i++
			if i >= len(args) {
				fmt.Fprintln(os.Stderr, "error: --path requires a directory")
				return 2
			}
			searchPaths = append(searchPaths, args[i])
		default:
			targets = append(targets, args[i])
		}
	}

	if len(targets) == 0 {
		var err error
		targets, err = defaultCheckTargets()
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
	}

	files, err := collectCheckFiles(targets)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}
	if len(files) == 0 {
		fmt.Println("no BASL files found")
		return 0
	}

	exitCode := 0
	for _, path := range files {
		resolvedSearchPaths, err := resolveScriptSearchPaths(path, searchPaths)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error[%s]: %s\n", path, err)
			exitCode = 1
			continue
		}

		diagnostics, err := checker.CheckFile(path, resolvedSearchPaths)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error[%s]: %s\n", path, err)
			exitCode = 1
			continue
		}
		for _, diag := range diagnostics {
			fmt.Fprintln(os.Stderr, diag.String())
			exitCode = 1
		}
	}

	return exitCode
}

func defaultCheckTargets() ([]string, error) {
	cwd, err := os.Getwd()
	if err != nil {
		return []string{"."}, err
	}

	projectRoot, ok, err := findProjectRoot(cwd)
	if err != nil {
		return nil, err
	}
	if ok && projectRoot == cwd {
		var targets []string
		mainPath := filepath.Join(projectRoot, "main.basl")
		if info, err := os.Stat(mainPath); err == nil && !info.IsDir() {
			targets = append(targets, mainPath)
		}
		for _, dir := range []string{
			filepath.Join(projectRoot, "lib"),
			filepath.Join(projectRoot, "test"),
		} {
			if info, err := os.Stat(dir); err == nil && info.IsDir() {
				targets = append(targets, dir)
			}
		}
		if len(targets) > 0 {
			return targets, nil
		}
	}

	return []string{"."}, nil
}

func collectCheckFiles(targets []string) ([]string, error) {
	seen := make(map[string]struct{})
	var files []string

	addFile := func(path string) error {
		abs, err := filepath.Abs(path)
		if err != nil {
			return err
		}
		if _, ok := seen[abs]; ok {
			return nil
		}
		seen[abs] = struct{}{}
		files = append(files, abs)
		return nil
	}

	for _, target := range targets {
		if strings.HasSuffix(target, "/...") {
			dir := strings.TrimSuffix(target, "/...")
			if dir == "" {
				dir = "."
			}
			if err := walkBaslFiles(dir, addFile); err != nil {
				return nil, err
			}
			continue
		}

		info, err := os.Stat(target)
		if err != nil {
			return nil, err
		}
		if info.IsDir() {
			if err := walkBaslFiles(target, addFile); err != nil {
				return nil, err
			}
			continue
		}
		if !strings.HasSuffix(target, ".basl") {
			return nil, fmt.Errorf("not a .basl file: %s", target)
		}
		if err := addFile(target); err != nil {
			return nil, err
		}
	}

	sort.Strings(files)
	return files, nil
}

func walkBaslFiles(root string, add func(string) error) error {
	return filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		if !strings.HasSuffix(path, ".basl") {
			return nil
		}
		return add(path)
	})
}
