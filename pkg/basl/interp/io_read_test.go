package interp

import (
	"os"
	"testing"
)

func TestIORead(t *testing.T) {
	tests := []struct {
		name     string
		input    string
		code     string
		expected int
	}{
		{
			name:  "read small chunk",
			input: "hello",
			code: `
				import "io";
				import "fmt";
				fn main() -> i32 {
					string data, err e = io.read(10);
					if (e != ok) {
						return 1;
					}
					if (data == "hello") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name:  "read exact size",
			input: "test",
			code: `
				import "io";
				fn main() -> i32 {
					string data, err e = io.read(4);
					if (e != ok) {
						return 1;
					}
					if (data.len() == 4) {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name:  "read in chunks",
			input: "abcdefghij",
			code: `
				import "io";
				fn main() -> i32 {
					string chunk1, err e1 = io.read(5);
					if (e1 != ok || chunk1 != "abcde") {
						return 1;
					}
					string chunk2, err e2 = io.read(5);
					if (e2 != ok || chunk2 != "fghij") {
						return 1;
					}
					return 0;
				}
			`,
			expected: 0,
		},
		{
			name:  "EOF detection",
			input: "x",
			code: `
				import "io";
				fn main() -> i32 {
					string data1, err e1 = io.read(10);
					if (e1 != ok || data1 != "x") {
						return 1;
					}
					string data2, err e2 = io.read(10);
					if (e2 != ok || data2 != "") {
						return 1;
					}
					return 0;
				}
			`,
			expected: 0,
		},
		{
			name:  "short read before EOF",
			input: "test",
			code: `
				import "io";
				fn main() -> i32 {
					// Request 100 bytes but only 4 available
					string data, err e = io.read(100);
					if (e != ok) {
						return 1;
					}
					// Should get short read with ok
					if (data.len() > 0 && data.len() <= 100) {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Save original stdin
			oldStdin := os.Stdin
			defer func() { os.Stdin = oldStdin }()

			// Create pipe for stdin
			r, w, err := os.Pipe()
			if err != nil {
				t.Fatal(err)
			}
			os.Stdin = r

			// Write test input
			go func() {
				w.Write([]byte(tt.input))
				w.Close()
			}()

			// Run test
			exitCode, _, err := evalBASL(tt.code)
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if exitCode != tt.expected {
				t.Errorf("expected exit code %d, got %d", tt.expected, exitCode)
			}
		})
	}
}

func TestIOReadShortReads(t *testing.T) {
	// Test that short reads can happen before EOF
	oldStdin := os.Stdin
	defer func() { os.Stdin = oldStdin }()

	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	os.Stdin = r

	// Write data in small increments with delays
	go func() {
		w.Write([]byte("abc"))
		w.Write([]byte("def"))
		w.Close()
	}()

	code := `
		import "io";
		fn main() -> i32 {
			i32 total = 0;
			while (true) {
				string chunk, err e = io.read(100);
				if (e != ok) {
					return 1;
				}
				if (chunk.len() == 0) {
					break;
				}
				total += chunk.len();
			}
			if (total == 6) {
				return 0;
			}
			return 1;
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

func TestIOReadErrors(t *testing.T) {
	tests := []struct {
		name        string
		code        string
		expectError bool
	}{
		{
			name: "negative count",
			code: `
				import "io";
				fn main() -> i32 {
					string data, err e = io.read(-1);
					return 0;
				}
			`,
			expectError: true,
		},
		{
			name: "zero count",
			code: `
				import "io";
				fn main() -> i32 {
					string data, err e = io.read(0);
					return 0;
				}
			`,
			expectError: true,
		},
		{
			name: "wrong argument type",
			code: `
				import "io";
				fn main() -> i32 {
					string data, err e = io.read("10");
					return 0;
				}
			`,
			expectError: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Provide dummy stdin
			oldStdin := os.Stdin
			defer func() { os.Stdin = oldStdin }()

			r, w, err := os.Pipe()
			if err != nil {
				t.Fatal(err)
			}
			os.Stdin = r
			w.Write([]byte("test"))
			w.Close()

			_, _, err = evalBASL(tt.code)
			if tt.expectError && err == nil {
				t.Error("expected error but got none")
			}
			if !tt.expectError && err != nil {
				t.Errorf("unexpected error: %v", err)
			}
		})
	}
}
