package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Dep represents a dependency entry in basl.toml.
type Dep struct {
	Name string
	Git  string
	Tag  string // tag or commit
}

// Manifest represents a parsed basl.toml.
type Manifest struct {
	Name    string
	Version string
	Deps    []Dep
	path    string // file path
}

func loadManifest(projectRoot string) (*Manifest, error) {
	path := filepath.Join(projectRoot, "basl.toml")
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("no basl.toml found (run from a BASL project root)")
	}
	m := &Manifest{path: path}
	inDeps := false
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if line == "[deps]" {
			inDeps = true
			continue
		}
		if strings.HasPrefix(line, "[") {
			inDeps = false
			continue
		}
		if inDeps {
			dep, err := parseDep(line)
			if err == nil {
				m.Deps = append(m.Deps, dep)
			}
		} else {
			k, v := parseKV(line)
			switch k {
			case "name":
				m.Name = v
			case "version":
				m.Version = v
			}
		}
	}
	return m, nil
}

func (m *Manifest) save() error {
	var b strings.Builder
	fmt.Fprintf(&b, "name = %q\n", m.Name)
	fmt.Fprintf(&b, "version = %q\n", m.Version)
	if len(m.Deps) > 0 {
		b.WriteString("\n[deps]\n")
		for _, d := range m.Deps {
			fmt.Fprintf(&b, "%s = { git = %q, tag = %q }\n", d.Name, d.Git, d.Tag)
		}
	}
	return os.WriteFile(m.path, []byte(b.String()), 0644)
}

func (m *Manifest) addDep(d Dep) {
	for i, existing := range m.Deps {
		if existing.Name == d.Name {
			m.Deps[i] = d
			return
		}
	}
	m.Deps = append(m.Deps, d)
}

func (m *Manifest) removeDep(name string) bool {
	for i, d := range m.Deps {
		if d.Name == name {
			m.Deps = append(m.Deps[:i], m.Deps[i+1:]...)
			return true
		}
	}
	return false
}

func (m *Manifest) getDep(name string) (Dep, bool) {
	for _, d := range m.Deps {
		if d.Name == name {
			return d, true
		}
	}
	return Dep{}, false
}

func parseDep(line string) (Dep, error) {
	// name = { git = "url", tag = "v1.0" }
	parts := strings.SplitN(line, "=", 2)
	if len(parts) != 2 {
		return Dep{}, fmt.Errorf("invalid dep line")
	}
	name := strings.TrimSpace(parts[0])
	rest := strings.TrimSpace(parts[1])

	d := Dep{Name: name}
	// Simple inline table parser
	rest = strings.TrimPrefix(rest, "{")
	rest = strings.TrimSuffix(rest, "}")
	for _, field := range strings.Split(rest, ",") {
		kv := strings.SplitN(strings.TrimSpace(field), "=", 2)
		if len(kv) != 2 {
			continue
		}
		k := strings.TrimSpace(kv[0])
		v := unquote(strings.TrimSpace(kv[1]))
		switch k {
		case "git":
			d.Git = v
		case "tag":
			d.Tag = v
		}
	}
	if d.Git == "" {
		return Dep{}, fmt.Errorf("missing git url")
	}
	return d, nil
}

func parseKV(line string) (string, string) {
	parts := strings.SplitN(line, "=", 2)
	if len(parts) != 2 {
		return "", ""
	}
	return strings.TrimSpace(parts[0]), unquote(strings.TrimSpace(parts[1]))
}

func unquote(s string) string {
	if len(s) >= 2 && s[0] == '"' && s[len(s)-1] == '"' {
		return s[1 : len(s)-1]
	}
	return s
}

// Lock file

type LockEntry struct {
	Name   string
	Git    string
	Tag    string
	Commit string
}

func loadLock(projectRoot string) map[string]LockEntry {
	path := filepath.Join(projectRoot, "basl.lock")
	data, err := os.ReadFile(path)
	if err != nil {
		return make(map[string]LockEntry)
	}
	entries := make(map[string]LockEntry)
	var current string
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			current = line[1 : len(line)-1]
			continue
		}
		if current == "" {
			continue
		}
		k, v := parseKV(line)
		e := entries[current]
		e.Name = current
		switch k {
		case "git":
			e.Git = v
		case "tag":
			e.Tag = v
		case "commit":
			e.Commit = v
		}
		entries[current] = e
	}
	return entries
}

func saveLock(projectRoot string, entries map[string]LockEntry) error {
	path := filepath.Join(projectRoot, "basl.lock")
	if len(entries) == 0 {
		os.Remove(path)
		return nil
	}
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	w := bufio.NewWriter(f)
	for _, e := range entries {
		fmt.Fprintf(w, "[%s]\n", e.Name)
		fmt.Fprintf(w, "git = %q\n", e.Git)
		fmt.Fprintf(w, "tag = %q\n", e.Tag)
		fmt.Fprintf(w, "commit = %q\n\n", e.Commit)
	}
	return w.Flush()
}
