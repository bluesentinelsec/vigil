package interp

import "testing"

func TestTernaryOperator(t *testing.T) {
	code := `
		import "fmt";
		
		fn main() -> i32 {
			i32 x = 5;
			string result = x > 3 ? "big" : "small";
			if (result != "big") {
				return 1;
			}
			
			i32 max = x > 10 ? x : 10;
			if (max != 10) {
				return 2;
			}
			
			// Nested ternary
			string category = x < 3 ? "small" : (x < 7 ? "medium" : "large");
			if (category != "medium") {
				return 3;
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

func TestTernaryWithBoolCondition(t *testing.T) {
	code := `
		fn main() -> i32 {
			bool flag = true;
			i32 value = flag ? 42 : 0;
			if (value != 42) {
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

func TestTernaryNonBoolCondition(t *testing.T) {
	code := `
		fn main() -> i32 {
			i32 x = 5;
			i32 value = x ? 42 : 0;
			return 0;
		}
	`

	_, _, err := evalBASL(code)
	if err == nil {
		t.Fatal("expected error for non-bool condition")
	}
	if !contains(err.Error(), "ternary condition must be bool") {
		t.Errorf("expected ternary condition error, got: %v", err)
	}
}
