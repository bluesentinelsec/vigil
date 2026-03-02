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

func TestTernaryShortCircuit(t *testing.T) {
	// Test that false branch is not evaluated when condition is true
	code := `
		fn explode() -> i32 {
			i32 x = 1 / 0;  // Would cause division by zero
			return x;
		}
		
		fn main() -> i32 {
			bool flag = true;
			i32 value = flag ? 42 : explode();
			if (value != 42) {
				return 1;
			}
			return 0;
		}
	`

	exitCode, _, err := evalBASL(code)
	if err != nil {
		t.Fatalf("unexpected error (false branch should not be evaluated): %v", err)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}

	// Test that true branch is not evaluated when condition is false
	code2 := `
		fn explode() -> i32 {
			i32 x = 1 / 0;  // Would cause division by zero
			return x;
		}
		
		fn main() -> i32 {
			bool flag = false;
			i32 value = flag ? explode() : 99;
			if (value != 99) {
				return 1;
			}
			return 0;
		}
	`

	exitCode, _, err = evalBASL(code2)
	if err != nil {
		t.Fatalf("unexpected error (true branch should not be evaluated): %v", err)
	}
	if exitCode != 0 {
		t.Errorf("expected exit code 0, got %d", exitCode)
	}
}

func TestTernaryChained(t *testing.T) {
	// Test chained ternary without parentheses (right-associative)
	code := `
		fn main() -> i32 {
			i32 x = 5;
			string category = x < 3 ? "small" : x < 7 ? "medium" : "large";
			if (category != "medium") {
				return 1;
			}
			
			x = 1;
			category = x < 3 ? "small" : x < 7 ? "medium" : "large";
			if (category != "small") {
				return 2;
			}
			
			x = 10;
			category = x < 3 ? "small" : x < 7 ? "medium" : "large";
			if (category != "large") {
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
