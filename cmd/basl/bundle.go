package main

import (
	"archive/zip"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"io/fs"
	"os"
	"sort"
	"testing/fstest"

	"github.com/bluesentinelsec/basl/pkg/basl/interp"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

func tryRunPackagedBinary(args []string) (bool, int) {
	fsys, _, ok, err := readSelfBundle()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[package]: %s\n", err)
		return true, 1
	}
	if !ok {
		return false, 0
	}

	entry, err := fs.ReadFile(fsys, packagedEntryPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[package]: %s\n", err)
		return true, 1
	}

	lex := lexer.New(string(entry))
	tokens, err := lex.Tokenize()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[lexer]: %s\n", err)
		return true, 1
	}

	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[parser]: %s\n", err)
		return true, 1
	}

	vm := interp.New()
	vm.RegisterScriptArgs(args)
	vm.RegisterEmbeddedFS("", fsys)

	code, err := vm.Exec(prog)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[runtime]: %s\n", err)
		return true, 1
	}
	return true, code
}

func inspectPackagedBinary(path string) (string, error) {
	_, names, ok, err := readBundleFromPath(path)
	if err != nil {
		return "", err
	}
	if !ok {
		return "", fmt.Errorf("%s is not a BASL packaged binary", path)
	}
	sort.Strings(names)

	var b bytes.Buffer
	b.WriteString("ENTRY\n")
	b.WriteString("  ")
	b.WriteString(packagedEntryPath)
	b.WriteString("\n\nFILES\n")
	for i, name := range names {
		if i > 0 {
			b.WriteString("\n")
		}
		b.WriteString("  ")
		b.WriteString(name)
	}
	return b.String(), nil
}

func readSelfBundle() (fstest.MapFS, []string, bool, error) {
	exePath, err := os.Executable()
	if err != nil {
		return nil, nil, false, err
	}
	return readBundleFromPath(exePath)
}

func readBundleFromPath(path string) (fstest.MapFS, []string, bool, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, nil, false, err
	}
	defer f.Close()

	trailerSize := len(packagedMagic) + 8
	info, err := f.Stat()
	if err != nil {
		return nil, nil, false, err
	}
	if info.Size() < int64(trailerSize) {
		return nil, nil, false, nil
	}

	trailer := make([]byte, trailerSize)
	if _, err := f.ReadAt(trailer, info.Size()-int64(trailerSize)); err != nil {
		return nil, nil, false, err
	}

	if string(trailer[8:]) != packagedMagic {
		return nil, nil, false, nil
	}

	bundleSize := binary.LittleEndian.Uint64(trailer[:8])
	if bundleSize == 0 {
		return nil, nil, false, fmt.Errorf("invalid package payload size")
	}
	if bundleSize > uint64(info.Size())-uint64(trailerSize) {
		return nil, nil, false, fmt.Errorf("invalid package payload trailer")
	}

	bundleStart := info.Size() - int64(trailerSize) - int64(bundleSize)
	archiveData := make([]byte, bundleSize)
	if _, err := f.ReadAt(archiveData, bundleStart); err != nil {
		return nil, nil, false, err
	}

	zr, err := zip.NewReader(bytes.NewReader(archiveData), int64(len(archiveData)))
	if err != nil {
		return nil, nil, false, err
	}

	files := make(fstest.MapFS, len(zr.File))
	names := make([]string, 0, len(zr.File))
	for _, zf := range zr.File {
		if zf.FileInfo().IsDir() {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			return nil, nil, false, err
		}
		body, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			return nil, nil, false, err
		}
		files[zf.Name] = &fstest.MapFile{Data: body, Mode: 0644}
		names = append(names, zf.Name)
	}

	return files, names, true, nil
}
