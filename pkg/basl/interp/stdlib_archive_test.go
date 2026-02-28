package interp

import (
	"path/filepath"
	"strings"
	"testing"
)

func TestArchiveTarCreateExtract(t *testing.T) {
	tmp := t.TempDir()
	src := strings.ReplaceAll(filepath.Join(tmp, "a.txt"), `\`, `\\`)
	tarFile := strings.ReplaceAll(filepath.Join(tmp, "out.tar"), `\`, `\\`)
	extractDir := strings.ReplaceAll(filepath.Join(tmp, "extracted"), `\`, `\\`)
	basl := `import "fmt"; import "file"; import "archive";
fn main() -> i32 {
	file.write_all("` + src + `", "hello tar");
	err e1 = archive.tar_create("` + tarFile + `", ["` + src + `"]);
	fmt.print(string(e1));
	err e2 = archive.tar_extract("` + tarFile + `", "` + extractDir + `");
	fmt.print(string(e2));
	return 0;
}`
	_, out, err := evalBASL(basl)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ok" || out[1] != "ok" {
		t.Fatalf("got %v", out)
	}
}

func TestArchiveZipCreateExtract(t *testing.T) {
	tmp := t.TempDir()
	src := strings.ReplaceAll(filepath.Join(tmp, "b.txt"), `\`, `\\`)
	zipFile := strings.ReplaceAll(filepath.Join(tmp, "out.zip"), `\`, `\\`)
	extractDir := strings.ReplaceAll(filepath.Join(tmp, "extracted"), `\`, `\\`)
	basl := `import "fmt"; import "file"; import "archive";
fn main() -> i32 {
	file.write_all("` + src + `", "hello zip");
	err e1 = archive.zip_create("` + zipFile + `", ["` + src + `"]);
	fmt.print(string(e1));
	err e2 = archive.zip_extract("` + zipFile + `", "` + extractDir + `");
	fmt.print(string(e2));
	return 0;
}`
	_, out, err := evalBASL(basl)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ok" || out[1] != "ok" {
		t.Fatalf("got %v", out)
	}
}
