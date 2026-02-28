package main

import (
	"archive/zip"
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/formatter"
	"github.com/bluesentinelsec/basl/pkg/basl/interp"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

const (
	packagedEntryPath = "entry.basl"
	packagedMagic     = "BASLPKG1"
)

type packagePlan struct {
	OutputPath string
	Files      map[string][]byte
}

type packageModule struct {
	absPath    string
	importName string
	bundlePath string
	rendered   []byte
}

type packageBuilder struct {
	searchRoots  []string
	builtin      map[string]struct{}
	modules      map[string]*packageModule
	nextImportID int
}

func runPackage(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("package"))
		return 0
	}

	cfg, err := parsePackageArgs(args)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 2
	}
	if cfg.inspectPath != "" {
		out, err := inspectPackagedBinary(cfg.inspectPath)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error[package]: %s\n", err)
			return 1
		}
		fmt.Print(out)
		if !strings.HasSuffix(out, "\n") {
			fmt.Println()
		}
		return 0
	}

	// Check if this is a library project (no main.basl)
	isLibrary, err := detectLibraryProject(cfg.entryPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[package]: %s\n", err)
		return 1
	}

	if isLibrary {
		// Package as library bundle
		return runLibraryBundle(cfg.entryPath, cfg.outputPath)
	}

	// Package as executable
	plan, err := buildPackagePlan(cfg.entryPath, cfg.outputPath, cfg.searchPaths)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[package]: %s\n", err)
		return 1
	}

	if err := buildStandaloneBinary(plan); err != nil {
		fmt.Fprintf(os.Stderr, "error[package]: %s\n", err)
		return 1
	}

	return 0
}

func detectLibraryProject(entryPath string) (bool, error) {
	targetPath := entryPath
	if targetPath == "" {
		cwd, err := os.Getwd()
		if err != nil {
			return false, err
		}
		targetPath = cwd
	}

	absTarget, err := filepath.Abs(targetPath)
	if err != nil {
		return false, err
	}

	info, err := os.Stat(absTarget)
	if err != nil {
		return false, err
	}

	// If it's a file, it's not a library
	if !info.IsDir() {
		return false, nil
	}

	// Check if it's a project directory
	projectRoot, ok, err := findProjectRoot(absTarget)
	if err != nil {
		return false, err
	}
	if !ok || projectRoot != absTarget {
		return false, nil
	}

	// Check if main.basl exists
	mainPath := filepath.Join(projectRoot, "main.basl")
	if _, err := os.Stat(mainPath); err == nil {
		return false, nil // Has main.basl, it's an application
	}

	// No main.basl, it's a library
	return true, nil
}

type packageArgs struct {
	entryPath   string
	inspectPath string
	outputPath  string
	searchPaths []string
}

func parsePackageArgs(args []string) (packageArgs, error) {
	var cfg packageArgs
	var positional []string
	inspect := false

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--inspect":
			inspect = true
		case "-o", "--output":
			i++
			if i >= len(args) {
				return cfg, fmt.Errorf("-o requires a path")
			}
			cfg.outputPath = args[i]
		case "--path":
			i++
			if i >= len(args) {
				return cfg, fmt.Errorf("--path requires a directory")
			}
			cfg.searchPaths = append(cfg.searchPaths, args[i])
		default:
			positional = append(positional, args[i])
		}
	}

	if inspect {
		if cfg.outputPath != "" {
			return cfg, fmt.Errorf("--inspect cannot be combined with -o")
		}
		if len(cfg.searchPaths) > 0 {
			return cfg, fmt.Errorf("--inspect cannot be combined with --path")
		}
		if len(positional) != 1 {
			return cfg, fmt.Errorf("usage: basl package --inspect <binary>")
		}
		cfg.inspectPath = positional[0]
		return cfg, nil
	}

	if len(positional) > 1 {
		return cfg, fmt.Errorf("usage: basl package [-o output] [--path dir] [<entry.basl|project-dir>]")
	}
	if len(positional) == 1 {
		cfg.entryPath = positional[0]
	}

	return cfg, nil
}

func buildPackagePlan(entryPath, outputPath string, searchPaths []string) (*packagePlan, error) {
	target, err := resolvePackageTarget(entryPath, searchPaths)
	if err != nil {
		return nil, err
	}
	if outputPath == "" {
		outputPath = target.DefaultOutput
	}

	builder := &packageBuilder{
		searchRoots: target.SearchRoots,
		builtin:     builtinModuleSet(),
		modules:     make(map[string]*packageModule),
	}

	if _, err := builder.processModule(target.EntryPath, ""); err != nil {
		return nil, err
	}

	files := make(map[string][]byte, len(builder.modules))
	for _, mod := range builder.modules {
		if mod.rendered == nil {
			return nil, fmt.Errorf("internal error: module %s was not rendered", mod.absPath)
		}
		files[mod.bundlePath] = mod.rendered
	}

	outAbs, err := filepath.Abs(outputPath)
	if err != nil {
		return nil, err
	}

	return &packagePlan{
		OutputPath: outAbs,
		Files:      files,
	}, nil
}

func buildStandaloneBinary(plan *packagePlan) error {
	exePath, err := os.Executable()
	if err != nil {
		return err
	}

	exeData, err := os.ReadFile(exePath)
	if err != nil {
		return err
	}
	info, err := os.Stat(exePath)
	if err != nil {
		return err
	}

	bundleData, err := renderPackageArchive(plan.Files)
	if err != nil {
		return err
	}

	out, err := os.OpenFile(plan.OutputPath, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, info.Mode())
	if err != nil {
		return err
	}
	defer out.Close()

	if _, err := out.Write(exeData); err != nil {
		return err
	}
	if _, err := out.Write(bundleData); err != nil {
		return err
	}

	var lenBuf [8]byte
	binary.LittleEndian.PutUint64(lenBuf[:], uint64(len(bundleData)))
	if _, err := out.Write(lenBuf[:]); err != nil {
		return err
	}
	if _, err := out.WriteString(packagedMagic); err != nil {
		return err
	}

	return out.Close()
}

func renderPackageArchive(files map[string][]byte) ([]byte, error) {
	var buf bytes.Buffer
	zw := zip.NewWriter(&buf)

	paths := make([]string, 0, len(files))
	for path := range files {
		paths = append(paths, path)
	}
	sort.Strings(paths)

	for _, path := range paths {
		w, err := zw.Create(path)
		if err != nil {
			return nil, err
		}
		if _, err := w.Write(files[path]); err != nil {
			return nil, err
		}
	}

	if err := zw.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func (b *packageBuilder) processModule(absPath, importName string) (*packageModule, error) {
	if mod, ok := b.modules[absPath]; ok {
		return mod, nil
	}

	bundlePath := packagedEntryPath
	if importName != "" {
		bundlePath = importName + ".basl"
	}

	mod := &packageModule{
		absPath:    absPath,
		importName: importName,
		bundlePath: bundlePath,
	}
	b.modules[absPath] = mod

	src, err := os.ReadFile(absPath)
	if err != nil {
		return nil, err
	}

	lexWithComments := lexer.New(string(src))
	tokensWithComments, err := lexWithComments.TokenizeWithComments()
	if err != nil {
		return nil, fmt.Errorf("%s: lexer: %w", absPath, err)
	}

	lex := lexer.New(string(src))
	tokens, err := lex.Tokenize()
	if err != nil {
		return nil, fmt.Errorf("%s: lexer: %w", absPath, err)
	}

	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		return nil, fmt.Errorf("%s: parser: %w", absPath, err)
	}

	for _, decl := range prog.Decls {
		imp, ok := decl.(*ast.ImportDecl)
		if !ok {
			continue
		}
		if _, ok := b.builtin[imp.Path]; ok {
			continue
		}

		origPath := imp.Path
		depAbs, err := resolvePackageImport(origPath, b.searchRoots)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", absPath, err)
		}

		depImportName := b.importNameFor(depAbs)
		if imp.Alias == "" {
			imp.Alias = defaultImportAlias(origPath)
		}
		imp.Path = depImportName

		if _, err := b.processModule(depAbs, depImportName); err != nil {
			return nil, err
		}
	}

	mod.rendered = formatter.Format(prog, tokensWithComments)
	return mod, nil
}

func (b *packageBuilder) importNameFor(absPath string) string {
	if mod, ok := b.modules[absPath]; ok && mod.importName != "" {
		return mod.importName
	}
	b.nextImportID++
	return fmt.Sprintf("pkg/mod%03d", b.nextImportID)
}

func resolvePackageImport(name string, roots []string) (string, error) {
	fileName := filepath.FromSlash(name) + ".basl"
	for _, root := range roots {
		fullPath := filepath.Join(root, fileName)
		absPath, err := filepath.Abs(fullPath)
		if err != nil {
			continue
		}
		if info, err := os.Stat(absPath); err == nil && !info.IsDir() {
			return absPath, nil
		}
	}
	return "", fmt.Errorf("module %q not found for packaging", name)
}

func defaultImportAlias(name string) string {
	parts := strings.Split(name, "/")
	if len(parts) == 0 {
		return name
	}
	return parts[len(parts)-1]
}

func builtinModuleSet() map[string]struct{} {
	out := make(map[string]struct{})
	for _, name := range interp.BuiltinModuleNames() {
		out[name] = struct{}{}
	}
	return out
}
