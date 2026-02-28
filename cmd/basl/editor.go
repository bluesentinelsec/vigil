package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"

	editorassets "github.com/bluesentinelsec/basl/editors"
)

type editorFileSpec struct {
	AssetPath string
	RelPath   string
}

type editorSpec struct {
	Name       string
	Summary    string
	BaseParts  []string
	RemoveRoot bool
	Files      []editorFileSpec
}

var editorOrder = []string{"vim", "nvim", "vscode"}

var editorSpecs = map[string]editorSpec{
	"vim": {
		Name:      "vim",
		Summary:   "Install BASL syntax files into ~/.vim",
		BaseParts: []string{".vim"},
		Files: []editorFileSpec{
			{AssetPath: "vim/ftdetect/basl.vim", RelPath: "ftdetect/basl.vim"},
			{AssetPath: "vim/syntax/basl.vim", RelPath: "syntax/basl.vim"},
		},
	},
	"nvim": {
		Name:      "nvim",
		Summary:   "Install BASL syntax files into ~/.config/nvim",
		BaseParts: []string{".config", "nvim"},
		Files: []editorFileSpec{
			{AssetPath: "vim/ftdetect/basl.vim", RelPath: "ftdetect/basl.vim"},
			{AssetPath: "vim/syntax/basl.vim", RelPath: "syntax/basl.vim"},
		},
	},
	"vscode": {
		Name:       "vscode",
		Summary:    "Install the bundled BASL extension into ~/.vscode/extensions/basl",
		BaseParts:  []string{".vscode", "extensions", "basl"},
		RemoveRoot: true,
		Files: []editorFileSpec{
			{AssetPath: "vscode/package.json", RelPath: "package.json"},
			{AssetPath: "vscode/extension.js", RelPath: "extension.js"},
			{AssetPath: "vscode/completions.json", RelPath: "completions.json"},
			{AssetPath: "vscode/language-configuration.json", RelPath: "language-configuration.json"},
			{AssetPath: "vscode/snippets/basl.json", RelPath: "snippets/basl.json"},
			{AssetPath: "vscode/syntaxes/basl.tmLanguage.json", RelPath: "syntaxes/basl.tmLanguage.json"},
		},
	},
}

type editorInstallFile struct {
	Path string
	Data []byte
}

type editorPlan struct {
	Name       string
	BaseDir    string
	RemoveRoot bool
	Files      []editorInstallFile
}

func runEditor(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("editor"))
		return 0
	}
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, "usage: basl editor <list|install|uninstall> [options]")
		return 2
	}

	switch args[0] {
	case "list":
		if len(args) != 1 {
			fmt.Fprintln(os.Stderr, "usage: basl editor list")
			return 2
		}
		fmt.Print(renderEditorList())
		return 0
	case "install":
		names, homeDir, force, err := parseEditorInstallArgs(args[1:])
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 2
		}
		if err := installEditors(names, homeDir, force, os.Stdout); err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		return 0
	case "uninstall":
		names, homeDir, err := parseEditorUninstallArgs(args[1:])
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 2
		}
		if err := uninstallEditors(names, homeDir, os.Stdout); err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			return 1
		}
		return 0
	default:
		fmt.Fprintf(os.Stderr, "error: unknown editor subcommand %q\n", args[0])
		fmt.Fprintln(os.Stderr, "usage: basl editor <list|install|uninstall> [options]")
		return 2
	}
}

func parseEditorInstallArgs(args []string) ([]string, string, bool, error) {
	homeDir := ""
	force := false
	var names []string

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--home":
			i++
			if i >= len(args) {
				return nil, "", false, fmt.Errorf("--home requires a directory")
			}
			homeDir = args[i]
		case "--force":
			force = true
		default:
			names = append(names, args[i])
		}
	}

	if len(names) == 0 {
		return nil, "", false, fmt.Errorf("usage: basl editor install [--home dir] [--force] <editor...>")
	}

	resolvedHome, err := resolveEditorHome(homeDir)
	if err != nil {
		return nil, "", false, err
	}
	return names, resolvedHome, force, nil
}

func parseEditorUninstallArgs(args []string) ([]string, string, error) {
	homeDir := ""
	var names []string

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--home":
			i++
			if i >= len(args) {
				return nil, "", fmt.Errorf("--home requires a directory")
			}
			homeDir = args[i]
		default:
			names = append(names, args[i])
		}
	}

	if len(names) == 0 {
		return nil, "", fmt.Errorf("usage: basl editor uninstall [--home dir] <editor...>")
	}

	resolvedHome, err := resolveEditorHome(homeDir)
	if err != nil {
		return nil, "", err
	}
	return names, resolvedHome, nil
}

func resolveEditorHome(homeDir string) (string, error) {
	if homeDir != "" {
		return filepath.Abs(homeDir)
	}
	return os.UserHomeDir()
}

func renderEditorList() string {
	var b strings.Builder
	b.WriteString("AVAILABLE EDITORS\n")
	for _, name := range editorOrder {
		spec := editorSpecs[name]
		fmt.Fprintf(&b, "  %-8s %s\n", spec.Name, spec.Summary)
	}
	return b.String()
}

func installEditors(names []string, homeDir string, force bool, out io.Writer) error {
	plans, err := buildEditorPlans(names, homeDir)
	if err != nil {
		return err
	}

	for _, plan := range plans {
		for _, file := range plan.Files {
			existing, err := os.ReadFile(file.Path)
			if err == nil {
				if bytes.Equal(existing, file.Data) {
					continue
				}
				if !force {
					return fmt.Errorf("%s already exists and differs; rerun with --force to overwrite", file.Path)
				}
			} else if !os.IsNotExist(err) {
				return err
			}

			if err := os.MkdirAll(filepath.Dir(file.Path), 0755); err != nil {
				return err
			}
			if err := os.WriteFile(file.Path, file.Data, 0644); err != nil {
				return err
			}
		}

		if out != nil {
			fmt.Fprintf(out, "installed %s in %s\n", plan.Name, plan.BaseDir)
		}
	}

	return nil
}

func uninstallEditors(names []string, homeDir string, out io.Writer) error {
	plans, err := buildEditorPlans(names, homeDir)
	if err != nil {
		return err
	}

	for _, plan := range plans {
		if plan.RemoveRoot {
			if err := os.RemoveAll(plan.BaseDir); err != nil {
				return err
			}
		} else {
			for _, file := range plan.Files {
				if err := os.Remove(file.Path); err != nil && !os.IsNotExist(err) {
					return err
				}
			}
			pruneEmptyParents(plan.Files, plan.BaseDir)
		}

		if out != nil {
			fmt.Fprintf(out, "removed %s from %s\n", plan.Name, plan.BaseDir)
		}
	}

	return nil
}

func buildEditorPlans(names []string, homeDir string) ([]editorPlan, error) {
	seen := make(map[string]bool)
	plans := make([]editorPlan, 0, len(names))

	for _, name := range names {
		spec, ok := editorSpecs[name]
		if !ok {
			return nil, fmt.Errorf("unknown editor %q (run `basl editor list` to see supported editors)", name)
		}
		if seen[name] {
			continue
		}
		seen[name] = true

		plan, err := buildEditorPlan(spec, homeDir)
		if err != nil {
			return nil, err
		}
		plans = append(plans, plan)
	}

	sort.Slice(plans, func(i, j int) bool {
		return plans[i].Name < plans[j].Name
	})
	return plans, nil
}

func buildEditorPlan(spec editorSpec, homeDir string) (editorPlan, error) {
	baseDir := filepath.Join(append([]string{homeDir}, spec.BaseParts...)...)
	plan := editorPlan{
		Name:       spec.Name,
		BaseDir:    baseDir,
		RemoveRoot: spec.RemoveRoot,
		Files:      make([]editorInstallFile, 0, len(spec.Files)),
	}

	for _, file := range spec.Files {
		data, err := editorassets.Assets.ReadFile(file.AssetPath)
		if err != nil {
			return editorPlan{}, err
		}
		plan.Files = append(plan.Files, editorInstallFile{
			Path: filepath.Join(baseDir, filepath.FromSlash(file.RelPath)),
			Data: data,
		})
	}

	return plan, nil
}

func pruneEmptyParents(files []editorInstallFile, stopDir string) {
	dirs := make(map[string]bool)
	for _, file := range files {
		dir := filepath.Dir(file.Path)
		for strings.HasPrefix(dir, stopDir) && dir != stopDir {
			if dirs[dir] {
				break
			}
			dirs[dir] = true
			dir = filepath.Dir(dir)
		}
	}

	dirList := make([]string, 0, len(dirs))
	for dir := range dirs {
		dirList = append(dirList, dir)
	}
	sort.Slice(dirList, func(i, j int) bool {
		return len(dirList[i]) > len(dirList[j])
	})

	for _, dir := range dirList {
		_ = os.Remove(dir)
	}
}
