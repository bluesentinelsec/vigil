package main

import (
	"archive/zip"
	"bytes"
	"encoding/binary"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestBuildPackagePlanRewritesImportsAndBundlesModules(t *testing.T) {
	root := t.TempDir()

	entry := writeDocLikeTestFile(t, root, "app/main.basl", `
import "fmt";
import "../shared/util";

fn main() -> i32 {
    fmt.print(util.answer());
    return 0;
}
`)
	writeDocLikeTestFile(t, root, "shared/util.basl", `
pub fn answer() -> string {
    return "ok";
}
`)

	plan, err := buildPackagePlan(entry, "", nil)
	if err != nil {
		t.Fatalf("buildPackagePlan() error = %v", err)
	}

	if got, want := filepath.Base(plan.OutputPath), "main"; got != want {
		t.Fatalf("output name mismatch: got %q want %q", got, want)
	}

	mainSrc, ok := plan.Files["entry.basl"]
	if !ok {
		t.Fatalf("missing entry file in package bundle: %v", mapsKeys(plan.Files))
	}

	mainText := string(mainSrc)
	if !strings.Contains(mainText, `import "fmt";`) {
		t.Fatalf("builtin import was rewritten unexpectedly:\n%s", mainText)
	}
	if !strings.Contains(mainText, `import "pkg/mod001" as util;`) {
		t.Fatalf("package import was not rewritten correctly:\n%s", mainText)
	}

	depSrc, ok := plan.Files["pkg/mod001.basl"]
	if !ok {
		t.Fatalf("missing packaged dependency: %v", mapsKeys(plan.Files))
	}
	if !strings.Contains(string(depSrc), `pub fn answer() -> string`) {
		t.Fatalf("dependency source missing expected function:\n%s", string(depSrc))
	}
}

func TestBuildPackagePlanProjectRootUsesMainAndLib(t *testing.T) {
	root := t.TempDir()

	writeDocLikeTestFile(t, root, "basl.toml", "name = \"demo\"\nversion = \"0.1.0\"\n")
	writeDocLikeTestFile(t, root, "main.basl", `
import "fmt";
import "util";

fn main() -> i32 {
    fmt.print(util.answer());
    return 0;
}
`)
	writeDocLikeTestFile(t, root, "lib/util.basl", `
pub fn answer() -> string {
    return "project";
}
`)

	plan, err := buildPackagePlan(root, "", nil)
	if err != nil {
		t.Fatalf("buildPackagePlan(project) error = %v", err)
	}

	if got, want := filepath.Base(plan.OutputPath), filepath.Base(root); got != want {
		t.Fatalf("project output name mismatch: got %q want %q", got, want)
	}

	mainText := string(plan.Files["entry.basl"])
	if !strings.Contains(mainText, `import "pkg/mod001" as util;`) {
		t.Fatalf("project import was not rewritten from lib/: \n%s", mainText)
	}
}

func TestRenderPackageArchiveIncludesBundledFiles(t *testing.T) {
	archiveData, err := renderPackageArchive(map[string][]byte{
		"entry.basl":      []byte("fn main() -> i32 { return 0; }\n"),
		"pkg/mod001.basl": []byte("pub fn greet() -> string { return \"hi\"; }\n"),
	})
	if err != nil {
		t.Fatalf("renderPackageArchive() error = %v", err)
	}

	zr, err := zip.NewReader(bytes.NewReader(archiveData), int64(len(archiveData)))
	if err != nil {
		t.Fatalf("zip.NewReader() error = %v", err)
	}

	files := make(map[string]string)
	for _, zf := range zr.File {
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("zf.Open() error = %v", err)
		}
		body, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("io.ReadAll() error = %v", err)
		}
		files[zf.Name] = string(body)
	}

	if got := files["entry.basl"]; !strings.Contains(got, "fn main") {
		t.Fatalf("archive missing entry.basl contents: %#v", files)
	}
	if got := files["pkg/mod001.basl"]; !strings.Contains(got, "pub fn greet") {
		t.Fatalf("archive missing module contents: %#v", files)
	}
}

func TestInspectPackagedBinaryListsBundledFiles(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, "app")

	archiveData, err := renderPackageArchive(map[string][]byte{
		"entry.basl":      []byte("fn main() -> i32 { return 0; }\n"),
		"pkg/mod001.basl": []byte("pub fn greet() -> string { return \"hi\"; }\n"),
	})
	if err != nil {
		t.Fatalf("renderPackageArchive() error = %v", err)
	}

	var data bytes.Buffer
	data.WriteString("stub-binary")
	data.Write(archiveData)
	var lenBuf [8]byte
	binary.LittleEndian.PutUint64(lenBuf[:], uint64(len(archiveData)))
	data.Write(lenBuf[:])
	data.WriteString(packagedMagic)

	if err := os.WriteFile(path, data.Bytes(), 0755); err != nil {
		t.Fatalf("os.WriteFile() error = %v", err)
	}

	out, err := inspectPackagedBinary(path)
	if err != nil {
		t.Fatalf("inspectPackagedBinary() error = %v", err)
	}
	for _, want := range []string{
		"ENTRY\n  entry.basl",
		"FILES",
		"  entry.basl",
		"  pkg/mod001.basl",
	} {
		if !strings.Contains(out, want) {
			t.Fatalf("inspect output missing %q:\n%s", want, out)
		}
	}
}

func TestPackageLibraryCreatesBundle(t *testing.T) {
	root := t.TempDir()

	// Create library project (no main.basl)
	writeDocLikeTestFile(t, root, "basl.toml", "name = \"mylib\"\nversion = \"1.0.0\"\n")
	writeDocLikeTestFile(t, root, "lib/utils.basl", `
pub fn hello() -> string {
    return "hello from mylib";
}
`)
	writeDocLikeTestFile(t, root, "lib/math/calc.basl", `
pub fn add(i32 a, i32 b) -> i32 {
    return a + b;
}
`)

	outputDir := filepath.Join(root, "output-bundle")

	// Run library bundle
	code := runLibraryBundle(root, outputDir)
	if code != 0 {
		t.Fatalf("runLibraryBundle() returned %d, want 0", code)
	}

	// Verify bundle structure
	if _, err := os.Stat(filepath.Join(outputDir, "basl.toml")); err != nil {
		t.Errorf("bundle missing basl.toml: %v", err)
	}
	if _, err := os.Stat(filepath.Join(outputDir, "README.txt")); err != nil {
		t.Errorf("bundle missing README.txt: %v", err)
	}
	if _, err := os.Stat(filepath.Join(outputDir, "lib/utils.basl")); err != nil {
		t.Errorf("bundle missing lib/utils.basl: %v", err)
	}
	if _, err := os.Stat(filepath.Join(outputDir, "lib/math/calc.basl")); err != nil {
		t.Errorf("bundle missing lib/math/calc.basl: %v", err)
	}

	// Verify file contents
	utilsData, err := os.ReadFile(filepath.Join(outputDir, "lib/utils.basl"))
	if err != nil {
		t.Fatalf("failed to read bundled utils.basl: %v", err)
	}
	if !strings.Contains(string(utilsData), "hello from mylib") {
		t.Errorf("bundled file missing expected content")
	}
}

func TestDetectLibraryProject(t *testing.T) {
	tests := []struct {
		name    string
		files   map[string]string
		wantLib bool
	}{
		{
			name: "application with main.basl",
			files: map[string]string{
				"basl.toml":    "name = \"app\"\n",
				"main.basl":    "fn main() -> i32 { return 0; }\n",
				"lib/mod.basl": "pub fn test() -> void {}\n",
			},
			wantLib: false,
		},
		{
			name: "library without main.basl",
			files: map[string]string{
				"basl.toml":    "name = \"lib\"\n",
				"lib/mod.basl": "pub fn test() -> void {}\n",
			},
			wantLib: true,
		},
		{
			name: "single file is not library",
			files: map[string]string{
				"script.basl": "fn main() -> i32 { return 0; }\n",
			},
			wantLib: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			root := t.TempDir()
			for path, content := range tt.files {
				writeDocLikeTestFile(t, root, path, content)
			}

			got, err := detectLibraryProject(root)
			if err != nil {
				t.Fatalf("detectLibraryProject() error = %v", err)
			}
			if got != tt.wantLib {
				t.Errorf("detectLibraryProject() = %v, want %v", got, tt.wantLib)
			}
		})
	}
}

func writeDocLikeTestFile(t *testing.T, root, relPath, contents string) string {
	t.Helper()

	path := filepath.Join(root, relPath)
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatalf("os.MkdirAll() error = %v", err)
	}
	data := strings.TrimLeft(contents, "\n") + "\n"
	if err := os.WriteFile(path, []byte(data), 0644); err != nil {
		t.Fatalf("os.WriteFile() error = %v", err)
	}
	return path
}

func mapsKeys[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	return keys
}
