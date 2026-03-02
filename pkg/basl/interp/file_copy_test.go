package interp

import (
	"testing"
)

func TestFileCopy(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			// Create source file
			err e1 = file.write_all("test_copy_src.txt", "hello world");
			if (e1 != ok) {
				return 1;
			}
			
			// Copy file
			err e2 = file.copy("test_copy_src.txt", "test_copy_dst.txt");
			if (e2 != ok) {
				file.remove("test_copy_src.txt");
				return 2;
			}
			
			// Verify destination
			string content, err e3 = file.read_all("test_copy_dst.txt");
			if (e3 != ok) {
				file.remove("test_copy_src.txt");
				file.remove("test_copy_dst.txt");
				return 3;
			}
			
			if (content != "hello world") {
				file.remove("test_copy_src.txt");
				file.remove("test_copy_dst.txt");
				return 4;
			}
			
			// Cleanup
			file.remove("test_copy_src.txt");
			file.remove("test_copy_dst.txt");
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

func TestFileCopyNonexistent(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			err e = file.copy("nonexistent_file_xyz.txt", "dest.txt");
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

func TestFileCopyOverwrite(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			// Create source and destination
			file.write_all("test_copy_src2.txt", "new content");
			file.write_all("test_copy_dst2.txt", "old content");
			
			// Copy (overwrite)
			err e = file.copy("test_copy_src2.txt", "test_copy_dst2.txt");
			if (e != ok) {
				file.remove("test_copy_src2.txt");
				file.remove("test_copy_dst2.txt");
				return 1;
			}
			
			// Verify overwrite
			string content, err e2 = file.read_all("test_copy_dst2.txt");
			if (content != "new content") {
				file.remove("test_copy_src2.txt");
				file.remove("test_copy_dst2.txt");
				return 2;
			}
			
			// Cleanup
			file.remove("test_copy_src2.txt");
			file.remove("test_copy_dst2.txt");
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
