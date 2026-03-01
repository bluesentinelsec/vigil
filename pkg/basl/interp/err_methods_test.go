package interp

import (
	"testing"
)

func TestErrIsEOF(t *testing.T) {
	code := `
		import "io";
		import "file";
		
		fn main() -> i32 {
			// Test with io.read_line EOF
			file.write_all("test_eof.txt", "");
			File f, err e1 = file.open("test_eof.txt", "r");
			if (e1 != ok) {
				return 1;
			}
			
			string line, err e2 = f.read_line();
			if (e2 == ok) {
				return 2;
			}
			
			if (!e2.is_eof()) {
				return 3;
			}
			
			f.close();
			file.remove("test_eof.txt");
			
			// Test with non-EOF error
			File f2, err e3 = file.open("nonexistent_file_xyz.txt", "r");
			if (e3 == ok) {
				return 4;
			}
			
			if (e3.is_eof()) {
				return 5;
			}
			
			// Test that user-created err("EOF") is NOT treated as EOF
			err e4 = err("EOF");
			if (e4.is_eof()) {
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
			File f, err e = file.open("nonexistent_file_xyz.txt", "r");
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

func TestErrIsEOFArityCheck(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			File f, err e = file.open("nonexistent_file_xyz.txt", "r");
			bool b = e.is_eof(123);
			return 0;
		}
	`

	_, _, err := evalBASL(code)
	if err == nil {
		t.Fatal("expected error for is_eof with arguments, got none")
	}
	if !contains(err.Error(), "expected 0 arguments") {
		t.Errorf("expected arity error, got: %v", err)
	}
}

func contains(s, substr string) bool {
	return len(s) >= len(substr) && (s == substr || len(s) > len(substr) && findSubstring(s, substr))
}

func findSubstring(s, substr string) bool {
	for i := 0; i <= len(s)-len(substr); i++ {
		if s[i:i+len(substr)] == substr {
			return true
		}
	}
	return false
}
