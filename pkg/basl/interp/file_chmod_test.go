package interp

import (
	"runtime"
	"testing"
)

func TestFileChmod(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("chmod has limited support on Windows")
	}

	code := `
		import "file";
		
		fn main() -> i32 {
			// Create file with default permissions
			err e1 = file.write_all("test_chmod.txt", "data");
			if (e1 != ok) {
				return 1;
			}
			
			// Change to 0600 (read/write owner only)
			err e2 = file.chmod("test_chmod.txt", 0o600);
			if (e2 != ok) {
				file.remove("test_chmod.txt");
				return 2;
			}
			
			// Verify mode changed
			FileStat info, err e3 = file.stat("test_chmod.txt");
			if (e3 != ok) {
				file.remove("test_chmod.txt");
				return 3;
			}
			
			// Mode should be 0600 (384 decimal) plus file type bits
			// On Unix, regular files have type bits, so we check lower 9 bits
			i32 perms = info.mode & 0o777;
			if (perms != 0o600) {
				file.remove("test_chmod.txt");
				return 4;
			}
			
			// Cleanup
			file.remove("test_chmod.txt");
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

func TestFileStatMode(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			// Create file
			err e1 = file.write_all("test_stat_mode.txt", "data");
			if (e1 != ok) {
				return 1;
			}
			
			// Get stat
			FileStat info, err e2 = file.stat("test_stat_mode.txt");
			if (e2 != ok) {
				file.remove("test_stat_mode.txt");
				return 2;
			}
			
			// Mode should be non-zero
			if (info.mode == 0) {
				file.remove("test_stat_mode.txt");
				return 3;
			}
			
			// Cleanup
			file.remove("test_stat_mode.txt");
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

func TestFileChmodExecutable(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("chmod has limited support on Windows")
	}

	code := `
		import "file";
		
		fn main() -> i32 {
			// Create file
			file.write_all("test_chmod_exec.txt", "#!/bin/sh\necho test");
			
			// Make executable (0755)
			err e = file.chmod("test_chmod_exec.txt", 0o755);
			if (e != ok) {
				file.remove("test_chmod_exec.txt");
				return 1;
			}
			
			// Verify
			FileStat info, err e2 = file.stat("test_chmod_exec.txt");
			if (e2 != ok) {
				file.remove("test_chmod_exec.txt");
				return 2;
			}
			
			i32 perms = info.mode & 0o777;
			if (perms != 0o755) {
				file.remove("test_chmod_exec.txt");
				return 3;
			}
			
			// Cleanup
			file.remove("test_chmod_exec.txt");
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

func TestFileChmodNonexistent(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			err e = file.chmod("nonexistent_file_xyz.txt", 0o644);
			if (e == ok) {
				return 1;
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
