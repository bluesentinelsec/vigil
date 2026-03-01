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
