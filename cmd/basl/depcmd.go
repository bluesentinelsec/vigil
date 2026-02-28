package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// --- basl get ---

func runGet(args []string) int {
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, "usage: basl get <git-url>[@<tag|commit>]")
		return 2
	}

	cwd, _ := os.Getwd()
	root, ok, _ := findProjectRoot(cwd)
	if !ok {
		fmt.Fprintln(os.Stderr, "error: not in a BASL project (no basl.toml found)")
		return 1
	}

	m, err := loadManifest(root)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}

	lock := loadLock(root)

	for _, arg := range args {
		gitURL, tag := parseGetArg(arg)
		name := depNameFromURL(gitURL)

		depDir := filepath.Join(root, "deps", name)

		commit, err := gitCloneOrFetch(gitURL, tag, depDir)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s: %s\n", name, err)
			return 1
		}

		if tag == "" {
			tag = "latest"
		}

		m.addDep(Dep{Name: name, Git: gitURL, Tag: tag})
		lock[name] = LockEntry{Name: name, Git: gitURL, Tag: tag, Commit: commit}

		fmt.Printf("added %s %s → deps/%s/\n", name, tag, name)
	}

	if err := m.save(); err != nil {
		fmt.Fprintf(os.Stderr, "error writing basl.toml: %s\n", err)
		return 1
	}
	if err := saveLock(root, lock); err != nil {
		fmt.Fprintf(os.Stderr, "error writing basl.lock: %s\n", err)
		return 1
	}
	return 0
}

// --- basl remove ---

func runRemove(args []string) int {
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, "usage: basl remove <package-name>")
		return 2
	}

	cwd, _ := os.Getwd()
	root, ok, _ := findProjectRoot(cwd)
	if !ok {
		fmt.Fprintln(os.Stderr, "error: not in a BASL project (no basl.toml found)")
		return 1
	}

	m, err := loadManifest(root)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}

	lock := loadLock(root)

	for _, name := range args {
		if !m.removeDep(name) {
			fmt.Fprintf(os.Stderr, "warning: %s not found in basl.toml\n", name)
			continue
		}
		delete(lock, name)
		depDir := filepath.Join(root, "deps", name)
		os.RemoveAll(depDir)
		fmt.Printf("removed %s\n", name)
	}

	if err := m.save(); err != nil {
		fmt.Fprintf(os.Stderr, "error writing basl.toml: %s\n", err)
		return 1
	}
	if err := saveLock(root, lock); err != nil {
		fmt.Fprintf(os.Stderr, "error writing basl.lock: %s\n", err)
		return 1
	}
	return 0
}

// --- basl upgrade ---

func runUpgrade(args []string) int {
	cwd, _ := os.Getwd()
	root, ok, _ := findProjectRoot(cwd)
	if !ok {
		fmt.Fprintln(os.Stderr, "error: not in a BASL project (no basl.toml found)")
		return 1
	}

	m, err := loadManifest(root)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}

	lock := loadLock(root)

	// Determine which deps to upgrade
	var targets []Dep
	if len(args) == 0 {
		// Upgrade all
		targets = m.Deps
	} else {
		for _, arg := range args {
			name, tag := parseGetArg(arg)
			// If arg looks like a name (no slashes), treat as dep name
			if !strings.Contains(name, "/") {
				d, ok := m.getDep(name)
				if !ok {
					fmt.Fprintf(os.Stderr, "error: %s not found in basl.toml\n", name)
					return 1
				}
				if tag != "" {
					d.Tag = tag
				}
				targets = append(targets, d)
			}
		}
	}

	for _, d := range targets {
		depDir := filepath.Join(root, "deps", d.Name)
		oldTag := d.Tag

		// Fetch latest
		newTag := d.Tag
		if newTag == "" || newTag == "latest" {
			latest, err := gitLatestTag(depDir)
			if err == nil && latest != "" {
				newTag = latest
			} else {
				newTag = "latest"
			}
		}

		commit, err := gitCloneOrFetch(d.Git, newTag, depDir)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s: %s\n", d.Name, err)
			return 1
		}

		m.addDep(Dep{Name: d.Name, Git: d.Git, Tag: newTag})
		lock[d.Name] = LockEntry{Name: d.Name, Git: d.Git, Tag: newTag, Commit: commit}

		if oldTag != newTag {
			fmt.Printf("upgraded %s %s → %s\n", d.Name, oldTag, newTag)
		} else {
			fmt.Printf("updated %s %s\n", d.Name, newTag)
		}
	}

	if err := m.save(); err != nil {
		fmt.Fprintf(os.Stderr, "error writing basl.toml: %s\n", err)
		return 1
	}
	if err := saveLock(root, lock); err != nil {
		fmt.Fprintf(os.Stderr, "error writing basl.lock: %s\n", err)
		return 1
	}
	return 0
}

// --- basl deps ---

func runDeps(args []string) int {
	cwd, _ := os.Getwd()
	root, ok, _ := findProjectRoot(cwd)
	if !ok {
		fmt.Fprintln(os.Stderr, "error: not in a BASL project (no basl.toml found)")
		return 1
	}

	m, err := loadManifest(root)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}

	lock := loadLock(root)

	if len(m.Deps) == 0 {
		fmt.Println("no dependencies")
		return 0
	}

	depsDir := filepath.Join(root, "deps")
	os.MkdirAll(depsDir, 0755)

	for _, d := range m.Deps {
		depDir := filepath.Join(depsDir, d.Name)

		// Prefer lock commit for reproducibility
		ref := d.Tag
		if le, ok := lock[d.Name]; ok && le.Commit != "" {
			ref = le.Commit
		}

		commit, err := gitCloneOrFetch(d.Git, ref, depDir)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s: %s\n", d.Name, err)
			return 1
		}

		lock[d.Name] = LockEntry{Name: d.Name, Git: d.Git, Tag: d.Tag, Commit: commit}
		fmt.Printf("fetched %s %s\n", d.Name, d.Tag)
	}

	// Clean deps not in manifest
	entries, _ := os.ReadDir(depsDir)
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		if _, ok := m.getDep(e.Name()); !ok {
			os.RemoveAll(filepath.Join(depsDir, e.Name()))
			fmt.Printf("removed stale %s\n", e.Name())
		}
	}

	if err := saveLock(root, lock); err != nil {
		fmt.Fprintf(os.Stderr, "error writing basl.lock: %s\n", err)
		return 1
	}
	return 0
}

// --- git helpers ---

func gitCloneOrFetch(gitURL, ref, destDir string) (string, error) {
	if _, err := os.Stat(filepath.Join(destDir, ".git")); err == nil {
		// Already cloned — fetch and checkout
		if err := gitRun(destDir, "fetch", "--tags", "--force", "origin"); err != nil {
			return "", fmt.Errorf("git fetch: %w", err)
		}
		checkoutRef := ref
		if checkoutRef == "" || checkoutRef == "latest" {
			checkoutRef = "origin/HEAD"
		}
		if err := gitRun(destDir, "checkout", "--force", checkoutRef); err != nil {
			// Try as origin/ref for branches
			gitRun(destDir, "checkout", "--force", "origin/"+checkoutRef)
		}
	} else {
		// Fresh clone
		cloneArgs := []string{"clone", gitURL, destDir}
		cmd := exec.Command("git", cloneArgs...)
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			return "", fmt.Errorf("git clone: %w", err)
		}
		if ref != "" && ref != "latest" {
			if err := gitRun(destDir, "checkout", "--force", ref); err != nil {
				return "", fmt.Errorf("git checkout %s: %w", ref, err)
			}
		}
	}
	return gitHead(destDir), nil
}

func gitRun(dir string, args ...string) error {
	cmd := exec.Command("git", args...)
	cmd.Dir = dir
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

func gitHead(dir string) string {
	cmd := exec.Command("git", "rev-parse", "HEAD")
	cmd.Dir = dir
	out, err := cmd.Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

func gitLatestTag(dir string) (string, error) {
	cmd := exec.Command("git", "describe", "--tags", "--abbrev=0", "origin/HEAD")
	cmd.Dir = dir
	out, err := cmd.Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(out)), nil
}

// --- URL helpers ---

func parseGetArg(arg string) (string, string) {
	// Split on @ for version: https://github.com/user/repo@v1.0
	if idx := strings.LastIndex(arg, "@"); idx > 0 {
		return arg[:idx], arg[idx+1:]
	}
	return arg, ""
}

func depNameFromURL(gitURL string) string {
	name := gitURL
	// Strip trailing .git
	name = strings.TrimSuffix(name, ".git")
	// Take last path segment
	if idx := strings.LastIndex(name, "/"); idx >= 0 {
		name = name[idx+1:]
	}
	// Sanitize
	return sanitizeDepName(name)
}

func sanitizeDepName(name string) string {
	var b strings.Builder
	for i, c := range name {
		if (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' {
			b.WriteRune(c)
		} else if c >= '0' && c <= '9' {
			if i == 0 {
				b.WriteRune('_')
			}
			b.WriteRune(c)
		} else {
			b.WriteRune('_')
		}
	}
	s := b.String()
	if s == "" {
		return "dep"
	}
	return s
}
