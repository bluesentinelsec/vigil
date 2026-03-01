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
					if (e2 == ok) {
						return 1;
					}
					return 0;
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
