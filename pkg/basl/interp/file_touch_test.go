package interp

import (
	"os"
	"testing"
	"time"
)

func TestFileTouchCreate(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			// Touch non-existent file (should create)
			err e1 = file.touch("test_touch_new.txt");
			if (e1 != ok) {
				return 1;
			}
			
			// Verify file exists
			bool exists = file.exists("test_touch_new.txt");
			if (!exists) {
				file.remove("test_touch_new.txt");
				return 2;
			}
			
			// Verify file is empty
			string content, err e2 = file.read_all("test_touch_new.txt");
			if (e2 != ok || content != "") {
				file.remove("test_touch_new.txt");
				return 3;
			}
			
			// Cleanup
			file.remove("test_touch_new.txt");
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

func TestFileTouchUpdate(t *testing.T) {
	code := `
		import "file";
		
		fn main() -> i32 {
			// Create file with content
			err e1 = file.write_all("test_touch_update.txt", "content");
			if (e1 != ok) {
				return 1;
			}
			
			// Get initial mod time
			FileStat info1, err e2 = file.stat("test_touch_update.txt");
			if (e2 != ok) {
				file.remove("test_touch_update.txt");
				return 2;
			}
			string time1 = info1.mod_time;
			
			// Touch file (update timestamp)
			err e3 = file.touch("test_touch_update.txt");
			if (e3 != ok) {
				file.remove("test_touch_update.txt");
				return 3;
			}
			
			// Verify content unchanged
			string content, err e4 = file.read_all("test_touch_update.txt");
			if (e4 != ok || content != "content") {
				file.remove("test_touch_update.txt");
				return 4;
			}
			
			// Get new mod time
			FileStat info2, err e5 = file.stat("test_touch_update.txt");
			if (e5 != ok) {
				file.remove("test_touch_update.txt");
				return 5;
			}
			string time2 = info2.mod_time;
			
			// Times should be different (note: may fail if system is very fast)
			// We just verify the touch succeeded, not that time changed
			// (timing tests are flaky)
			
			// Cleanup
			file.remove("test_touch_update.txt");
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

func TestFileTouchTimestampUpdate(t *testing.T) {
	// This test verifies timestamp actually changes at Go level
	code := `
		import "file";
		
		fn main() -> i32 {
			// Create file
			err e = file.write_all("test_touch_ts.txt", "data");
			if (e != ok) {
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
		t.Fatalf("failed to create test file")
	}

	// Get initial mod time
	info1, err := os.Stat("test_touch_ts.txt")
	if err != nil {
		t.Fatalf("failed to stat file: %v", err)
	}
	time1 := info1.ModTime()

	// Sleep to ensure time difference
	time.Sleep(10 * time.Millisecond)

	// Touch file
	code2 := `
		import "file";
		
		fn main() -> i32 {
			err e = file.touch("test_touch_ts.txt");
			if (e != ok) {
				return 1;
			}
			return 0;
		}
	`

	exitCode, _, err = evalBASL(code2)
	if err != nil {
		os.Remove("test_touch_ts.txt")
		t.Fatalf("unexpected error: %v", err)
	}
	if exitCode != 0 {
		os.Remove("test_touch_ts.txt")
		t.Fatalf("touch failed")
	}

	// Get new mod time
	info2, err := os.Stat("test_touch_ts.txt")
	if err != nil {
		os.Remove("test_touch_ts.txt")
		t.Fatalf("failed to stat file after touch: %v", err)
	}
	time2 := info2.ModTime()

	// Cleanup
	os.Remove("test_touch_ts.txt")

	// Verify timestamp changed
	if !time2.After(time1) {
		t.Errorf("expected mod time to increase, got %v -> %v", time1, time2)
	}
}
