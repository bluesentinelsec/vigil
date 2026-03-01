package interp

import (
	"runtime"
	"testing"
)

func TestFileSymlink(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("Symlinks require admin privileges on Windows")
	}

	code := `
		import "file";
		
		fn main() -> i32 {
			// Create target file
			err e1 = file.write_all("test_symlink_target.txt", "target content");
			if (e1 != ok) {
				return 1;
			}
			
			// Create symlink
			err e2 = file.symlink("test_symlink_target.txt", "test_symlink_link.txt");
			if (e2 != ok) {
				file.remove("test_symlink_target.txt");
				return 2;
			}
			
			// Read through symlink
			string content, err e3 = file.read_all("test_symlink_link.txt");
			if (e3 != ok || content != "target content") {
				file.remove("test_symlink_target.txt");
				file.remove("test_symlink_link.txt");
				return 3;
			}
			
			// Cleanup
			file.remove("test_symlink_link.txt");
			file.remove("test_symlink_target.txt");
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

func TestFileReadlink(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("Symlinks require admin privileges on Windows")
	}

	code := `
		import "file";
		
		fn main() -> i32 {
			// Create target and symlink
			file.write_all("test_readlink_target.txt", "data");
			err e1 = file.symlink("test_readlink_target.txt", "test_readlink_link.txt");
			if (e1 != ok) {
				file.remove("test_readlink_target.txt");
				return 1;
			}
			
			// Read symlink target
			string target, err e2 = file.readlink("test_readlink_link.txt");
			if (e2 != ok || target != "test_readlink_target.txt") {
				file.remove("test_readlink_target.txt");
				file.remove("test_readlink_link.txt");
				return 2;
			}
			
			// Cleanup
			file.remove("test_readlink_link.txt");
			file.remove("test_readlink_target.txt");
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

func TestFileHardlink(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			// Create target file
			err e1 = file.write_all("test_hardlink_target.txt", "shared content");
			if (e1 != ok) {
				return 1;
			}
			
			// Create hard link
			err e2 = file.link("test_hardlink_target.txt", "test_hardlink_link.txt");
			if (e2 != ok) {
				file.remove("test_hardlink_target.txt");
				return 2;
			}
			
			// Read through hard link
			string content, err e3 = file.read_all("test_hardlink_link.txt");
			if (e3 != ok || content != "shared content") {
				file.remove("test_hardlink_target.txt");
				file.remove("test_hardlink_link.txt");
				return 3;
			}
			
			// Modify through link
			err e4 = file.write_all("test_hardlink_link.txt", "modified");
			if (e4 != ok) {
				file.remove("test_hardlink_target.txt");
				file.remove("test_hardlink_link.txt");
				return 4;
			}
			
			// Verify original sees modification
			string content2, err e5 = file.read_all("test_hardlink_target.txt");
			if (e5 != ok || content2 != "modified") {
				file.remove("test_hardlink_target.txt");
				file.remove("test_hardlink_link.txt");
				return 5;
			}
			
			// Cleanup
			file.remove("test_hardlink_link.txt");
			file.remove("test_hardlink_target.txt");
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

func TestFileReadlinkNotSymlink(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			// Create regular file
			file.write_all("test_not_symlink.txt", "data");
			
			// Try to read as symlink (should fail)
			string target, err e = file.readlink("test_not_symlink.txt");
			if (e == ok) {
				file.remove("test_not_symlink.txt");
				return 1;
			}
			
			// Cleanup
			file.remove("test_not_symlink.txt");
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
