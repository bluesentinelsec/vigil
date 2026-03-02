package interp

import (
	"testing"
)

func TestStringComparison(t *testing.T) {
	tests := []struct {
		name     string
		code     string
		expected int
	}{
		{
			name: "less than",
			code: `
				import "fmt";
				fn main() -> i32 {
					if ("a" < "b") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name: "less than or equal",
			code: `
				import "fmt";
				fn main() -> i32 {
					if ("a" <= "a") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name: "greater than",
			code: `
				import "fmt";
				fn main() -> i32 {
					if ("b" > "a") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name: "greater than or equal",
			code: `
				import "fmt";
				fn main() -> i32 {
					if ("b" >= "b") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name: "character range check",
			code: `
				import "fmt";
				fn main() -> i32 {
					string ch = "m";
					if (ch >= "a" && ch <= "z") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name: "lexicographic ordering",
			code: `
				import "fmt";
				fn main() -> i32 {
					if ("apple" < "banana") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name: "UTF-8 byte ordering",
			code: `
				import "fmt";
				fn main() -> i32 {
					// UTF-8: é (0xC3 0xA9) vs e (0x65)
					// 0xC3 > 0x65, so "é" > "e"
					if ("é" > "e") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name: "empty string comparison",
			code: `
				import "fmt";
				fn main() -> i32 {
					if ("" < "a") {
						return 0;
					}
					return 1;
				}
			`,
			expected: 0,
		},
		{
			name: "equal strings with <=",
			code: `
				import "fmt";
				fn main() -> i32 {
					if ("test" <= "test") {
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

func TestStringComparisonErrors(t *testing.T) {
	tests := []struct {
		name        string
		code        string
		expectError bool
	}{
		{
			name: "mixed type string and int",
			code: `
				import "fmt";
				fn main() -> i32 {
					string s = "5";
					i32 n = 5;
					if (s < n) {
						return 0;
					}
					return 1;
				}
			`,
			expectError: true,
		},
		{
			name: "mixed type string and bool",
			code: `
				import "fmt";
				fn main() -> i32 {
					string s = "true";
					bool b = true;
					if (s < b) {
						return 0;
					}
					return 1;
				}
			`,
			expectError: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, _, err := evalBASL(tt.code)
			if tt.expectError && err == nil {
				t.Error("expected error but got none")
			}
			if !tt.expectError && err != nil {
				t.Errorf("unexpected error: %v", err)
			}
		})
	}
}
