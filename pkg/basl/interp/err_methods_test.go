package interp

import (
	"os"
	"testing"
)

func TestErrKind(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			// Test with file EOF
			file.write_all("test_eof.txt", "");
			file.File f, err e1 = file.open("test_eof.txt", "r");
			if (e1 != ok) {
				return 1;
			}
			
			string line, err e2 = f.read_line();
			if (e2 == ok) {
				return 2;
			}
			
			if (e2.kind() != err.eof) {
				return 3;
			}
			
			f.close();
			file.remove("test_eof.txt");
			
			// Test with not_found error
			file.File f2, err e3 = file.open("nonexistent_file_xyz.txt", "r");
			if (e3 == ok) {
				return 4;
			}
			
			if (e3.kind() != err.not_found) {
				return 5;
			}
			
			// Test user-created error with kind
			err e4 = err("bad input", err.arg);
			if (e4.kind() != err.arg) {
				return 6;
			}
			
			return 0;
		}
	`

	exitCode, _, err := evalBASL(code)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func TestErrMessage(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			file.File f, err e = file.open("nonexistent_file_xyz.txt", "r");
			if (e == ok) {
				return 1;
			}
			
			string msg = e.message();
			if (msg.len() == 0) {
				return 2;
			}
			
			return 0;
		}
	`

	exitCode, _, err := evalBASL(code)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func TestErrKindArityCheck(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			file.File f, err e = file.open("nonexistent_file_xyz.txt", "r");
			string k = e.kind(123);
			return 0;
		}
	`

	_, _, err := evalBASL(code)
	if err == nil {
		t.Fatal("expected error for kind with arguments, got none")
	}
	if !contains(err.Error(), "expected 0 arguments") {
		t.Errorf("expected arity error, got: %v", err)
	}
}

func TestErrMessageArityCheck(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			file.File f, err e = file.open("nonexistent_file_xyz.txt", "r");
			string msg = e.message(123);
			return 0;
		}
	`

	_, _, err := evalBASL(code)
	if err == nil {
		t.Fatal("expected error for message with arguments, got none")
	}
	if !contains(err.Error(), "expected 0 arguments") {
		t.Errorf("expected arity error, got: %v", err)
	}
}

func TestErrKindSwitch(t *testing.T) {
	code := `
		import "fmt";
		import "file";
		
		fn main() -> i32 {
			file.File f, err e = file.open("nonexistent_file_xyz.txt", "r");
			if (e != ok) {
				switch (e.kind()) {
					case err.not_found:
						fmt.print("not_found");
					case err.permission:
						fmt.print("permission");
					default:
						fmt.print("other");
				}
			}
			return 0;
		}
	`

	exitCode, out, err := evalBASL(code)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
	if len(out) == 0 || out[0] != "not_found" {
		t.Errorf("expected 'not_found', got %v", out)
	}
}

func TestErrInvalidKind(t *testing.T) {
	code := `
		fn main() -> i32 {
			err e = err("bad", "bogus");
			return 0;
		}
	`

	_, _, err := evalBASL(code)
	if err == nil {
		t.Fatal("expected error for invalid kind, got none")
	}
	if !contains(err.Error(), "unknown error kind") {
		t.Errorf("expected unknown kind error, got: %v", err)
	}
}

func TestIOReadStringEOF(t *testing.T) {
	oldStdin := os.Stdin
	defer func() { os.Stdin = oldStdin }()

	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	os.Stdin = r
	w.Close()

	code := `
		import "io";
		
		fn main() -> i32 {
			string val, err e = io.read_string("prompt: ");
			if (e == ok) {
				return 1;
			}
			if (e.kind() != err.eof) {
				return 2;
			}
			return 0;
		}
	`

	exitCode, _, evalErr := evalBASL(code)
	if evalErr != nil {
		t.Fatalf("unexpected error: %v", evalErr)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func TestIOReadI32EOF(t *testing.T) {
	oldStdin := os.Stdin
	defer func() { os.Stdin = oldStdin }()

	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	os.Stdin = r
	w.Close()

	code := `
		import "io";
		
		fn main() -> i32 {
			i32 val, err e = io.read_i32("prompt: ");
			if (e == ok) {
				return 1;
			}
			if (e.kind() != err.eof) {
				return 2;
			}
			return 0;
		}
	`

	exitCode, _, evalErr := evalBASL(code)
	if evalErr != nil {
		t.Fatalf("unexpected error: %v", evalErr)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func TestIOReadF64EOF(t *testing.T) {
	oldStdin := os.Stdin
	defer func() { os.Stdin = oldStdin }()

	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	os.Stdin = r
	w.Close()

	code := `
		import "io";
		
		fn main() -> i32 {
			f64 val, err e = io.read_f64("prompt: ");
			if (e == ok) {
				return 1;
			}
			if (e.kind() != err.eof) {
				return 2;
			}
			return 0;
		}
	`

	exitCode, _, evalErr := evalBASL(code)
	if evalErr != nil {
		t.Fatalf("unexpected error: %v", evalErr)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func contains(s, substr string) bool {
	for i := 0; i <= len(s)-len(substr); i++ {
		if s[i:i+len(substr)] == substr {
			return true
		}
	}
	return false
}

func TestTimeParseReturnsErrParse(t *testing.T) {
	src := `
import "time";
fn main() -> i32 {
    i64 ms, err e = time.parse("2006-01-02", "not-a-date");
    if (e == ok) {
        return 1;
    }
    if (e.kind() != err.parse) {
        return 2;
    }
    return 0;
}
`
	exitCode, _, err := evalBASL(src)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func TestBase64DecodeReturnsErrParse(t *testing.T) {
	src := `
import "base64";
fn main() -> i32 {
    string decoded, err e = base64.decode("%%%");
    if (e == ok) {
        return 1;
    }
    if (e.kind() != err.parse) {
        return 2;
    }
    return 0;
}
`
	exitCode, _, err := evalBASL(src)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func TestCsvParseReturnsErrParse(t *testing.T) {
	src := `
import "csv";
fn main() -> i32 {
    // Unterminated quote is malformed CSV
    array<array<string>> rows, err e = csv.parse("\"unterminated");
    if (e == ok) {
        return 1;
    }
    if (e.kind() != err.parse) {
        return 2;
    }
    return 0;
}
`
	exitCode, _, err := evalBASL(src)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func TestErrEmptyMessageRejected(t *testing.T) {
	src := `
fn main() -> i32 {
    err e = err("", err.io);
    return 0;
}
`
	_, _, err := evalBASL(src)
	if err == nil {
		t.Error("expected error for empty message")
	}
	if !contains(err.Error(), "empty") {
		t.Errorf("expected 'empty' in error, got: %s", err.Error())
	}
}
