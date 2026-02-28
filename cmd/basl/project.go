package main

import (
	"fmt"
	"os"
	"path/filepath"
)

func findProjectRoot(path string) (string, bool, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return "", false, err
	}

	start := absPath
	if info, err := os.Stat(absPath); err == nil && !info.IsDir() {
		start = filepath.Dir(absPath)
	}

	for {
		manifest := filepath.Join(start, "basl.toml")
		if info, err := os.Stat(manifest); err == nil && !info.IsDir() {
			return start, true, nil
		}
		parent := filepath.Dir(start)
		if parent == start {
			return "", false, nil
		}
		start = parent
	}
}

func projectImportRoots(projectRoot string) []string {
	return []string{
		filepath.Join(projectRoot, "lib"),
		filepath.Join(projectRoot, "deps"),
	}
}

func resolveScriptSearchPaths(scriptPath string, userSearchPaths []string) ([]string, error) {
	absScript, err := filepath.Abs(scriptPath)
	if err != nil {
		return nil, err
	}

	var paths []string
	addUniquePath(&paths, filepath.Dir(absScript))

	projectRoot, ok, err := findProjectRoot(absScript)
	if err != nil {
		return nil, err
	}
	if ok {
		for _, root := range projectImportRoots(projectRoot) {
			addUniquePath(&paths, root)
		}
	}

	for _, sp := range userSearchPaths {
		abs, err := filepath.Abs(sp)
		if err != nil {
			return nil, err
		}
		addUniquePath(&paths, abs)
	}

	return paths, nil
}

func resolveTestSearchPaths(testFilePath string) ([]string, error) {
	projectRoot, ok, err := findProjectRoot(testFilePath)
	if err != nil || !ok {
		return nil, err
	}

	var paths []string
	for _, root := range projectImportRoots(projectRoot) {
		addUniquePath(&paths, root)
	}
	return paths, nil
}

type packageTarget struct {
	EntryPath     string
	DefaultOutput string
	SearchRoots   []string
	ProjectRoot   string
	ProjectMode   bool
}

func resolvePackageTarget(entryArg string, extraSearchPaths []string) (packageTarget, error) {
	var target packageTarget

	targetPath := entryArg
	if targetPath == "" {
		cwd, err := os.Getwd()
		if err != nil {
			return target, err
		}
		targetPath = cwd
	}

	absTarget, err := filepath.Abs(targetPath)
	if err != nil {
		return target, err
	}

	info, err := os.Stat(absTarget)
	if err != nil {
		return target, err
	}

	if info.IsDir() {
		projectRoot, ok, err := findProjectRoot(absTarget)
		if err != nil {
			return target, err
		}
		if !ok || projectRoot != absTarget {
			return target, fmt.Errorf("%s is not a BASL project root (missing basl.toml)", targetPath)
		}
		entryPath := filepath.Join(projectRoot, "main.basl")
		if st, err := os.Stat(entryPath); err != nil || st.IsDir() {
			return target, fmt.Errorf("project %s has no main.basl to package", targetPath)
		}

		target.EntryPath = entryPath
		target.DefaultOutput = filepath.Base(projectRoot)
		target.ProjectRoot = projectRoot
		target.ProjectMode = true
		for _, root := range projectImportRoots(projectRoot) {
			addUniquePath(&target.SearchRoots, root)
		}
	} else {
		target.EntryPath = absTarget
		target.DefaultOutput = trimBaslExt(filepath.Base(absTarget))
		if target.DefaultOutput == "" {
			target.DefaultOutput = "app"
		}
		addUniquePath(&target.SearchRoots, filepath.Dir(absTarget))

		projectRoot, ok, err := findProjectRoot(absTarget)
		if err != nil {
			return target, err
		}
		if ok {
			target.ProjectRoot = projectRoot
			target.ProjectMode = true
			for _, root := range projectImportRoots(projectRoot) {
				addUniquePath(&target.SearchRoots, root)
			}
			if filepath.Base(absTarget) == "main.basl" && filepath.Dir(absTarget) == projectRoot {
				target.DefaultOutput = filepath.Base(projectRoot)
			}
		}
	}

	for _, sp := range extraSearchPaths {
		abs, err := filepath.Abs(sp)
		if err != nil {
			return target, err
		}
		addUniquePath(&target.SearchRoots, abs)
	}

	if target.DefaultOutput == "" {
		target.DefaultOutput = "app"
	}
	return target, nil
}

func defaultTestTargets() ([]string, error) {
	cwd, err := os.Getwd()
	if err != nil {
		return []string{"."}, err
	}

	projectRoot, ok, err := findProjectRoot(cwd)
	if err != nil {
		return nil, err
	}
	if ok && projectRoot == cwd {
		testDir := filepath.Join(projectRoot, "test")
		if info, err := os.Stat(testDir); err == nil && info.IsDir() {
			return []string{testDir}, nil
		}
	}

	return []string{"."}, nil
}

func addUniquePath(paths *[]string, path string) {
	if path == "" {
		return
	}
	for _, existing := range *paths {
		if existing == path {
			return
		}
	}
	*paths = append(*paths, path)
}

func trimBaslExt(name string) string {
	trimmed := name
	if filepath.Ext(trimmed) == ".basl" {
		trimmed = trimmed[:len(trimmed)-len(".basl")]
	}
	return trimmed
}
