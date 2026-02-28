package interp

import (
	"runtime"
	"testing"
)

func TestOsPlatform(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "os"; fn main() -> i32 { fmt.print(os.platform()); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != runtime.GOOS {
		t.Fatalf("got %q, want %q", out[0], runtime.GOOS)
	}
}

func TestOsCwd(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "os"; fn main() -> i32 { string d, err e = os.cwd(); fmt.print(string(e)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ok" {
		t.Fatalf("got %q", out[0])
	}
}

func TestOsEnv(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "os"; fn main() -> i32 { string v, bool found = os.env("PATH"); fmt.print(string(found)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" {
		t.Fatalf("got %q", out[0])
	}
}

func TestOsEnvMissing(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "os"; fn main() -> i32 { string v, bool found = os.env("BASL_NONEXISTENT_VAR_XYZ"); fmt.print(string(found)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "false" {
		t.Fatalf("got %q", out[0])
	}
}
