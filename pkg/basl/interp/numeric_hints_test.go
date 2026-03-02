package interp

import (
	"strings"
	"testing"
)

func TestNumericTypeMismatchHints(t *testing.T) {
	tests := []struct {
		name        string
		src         string
		wantErrPart string
		wantHint    string
	}{
		{
			name: "i32_vs_i64",
			src: `fn main() -> i32 {
				i32 a = 10;
				i64 b = i64(20);
				bool c = a < b;
				return 0;
			}`,
			wantErrPart: "cannot apply \"<\" to i32 and i64",
			wantHint:    "cast left operand: i64(left) < right",
		},
		{
			name: "u8_vs_u32",
			src: `fn main() -> i32 {
				u8 a = u8(10);
				u32 b = u32(20);
				bool c = a < b;
				return 0;
			}`,
			wantErrPart: "cannot apply \"<\" to u8 and u32",
			wantHint:    "cast left operand: u32(left) < right",
		},
		{
			name: "i64_vs_i32",
			src: `fn main() -> i32 {
				i64 a = i64(100);
				i32 b = 20;
				bool c = a > b;
				return 0;
			}`,
			wantErrPart: "cannot apply \">\" to i64 and i32",
			wantHint:    "cast right operand: left > i64(right)",
		},
		{
			name: "f64_vs_i32",
			src: `fn main() -> i32 {
				f64 a = 3.14;
				i32 b = 3;
				bool c = a == b;
				return 0;
			}`,
			wantErrPart: "cannot apply \"==\" to f64 and i32",
			wantHint:    "cast right operand: left == f64(right)",
		},
		{
			name: "signed_vs_unsigned_no_hint",
			src: `fn main() -> i32 {
				i32 a = -1;
				u32 b = u32(1);
				bool c = a < b;
				return 0;
			}`,
			wantErrPart: "cannot apply \"<\" to i32 and u32",
			wantHint:    "cast both operands to a common type (mixing signed/unsigned requires care)",
		},
		{
			name: "modulo_float_no_hint",
			src: `fn main() -> i32 {
				f64 a = 10.5;
				i32 b = 3;
				f64 c = a % b;
				return 0;
			}`,
			wantErrPart: "cannot apply \"%\" to f64 and i32",
			wantHint:    "", // No hint because % doesn't work on f64
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			interp := New()
			_, err := interp.EvalString(tt.src)
			if err == nil {
				t.Fatal("expected error, got none")
			}
			errStr := err.Error()
			if !strings.Contains(errStr, tt.wantErrPart) {
				t.Errorf("error missing expected part\ngot: %s\nwant substring: %s", errStr, tt.wantErrPart)
			}
			if tt.wantHint != "" {
				if !strings.Contains(errStr, tt.wantHint) {
					t.Errorf("error missing expected hint\ngot: %s\nwant substring: %s", errStr, tt.wantHint)
				}
			} else {
				// Verify no hint is present
				if strings.Contains(errStr, "hint:") {
					t.Errorf("unexpected hint in error\ngot: %s", errStr)
				}
			}
		})
	}
}
