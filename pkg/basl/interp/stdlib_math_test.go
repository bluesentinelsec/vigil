package interp

import "testing"

func TestMathSqrt(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.sqrt(4.0))); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "2" {
		t.Fatalf("got %q", out[0])
	}
}

func TestMathPow(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.pow(2.0, 10.0))); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "1024" {
		t.Fatalf("got %q", out[0])
	}
}

func TestMathMinMax(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.min(3.0, 7.0))); fmt.print(string(math.max(3.0, 7.0))); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "3" || out[1] != "7" {
		t.Fatalf("got %v", out)
	}
}

func TestMathConstants(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.pi > 3.14)); fmt.print(string(math.e > 2.71)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" || out[1] != "true" {
		t.Fatalf("got %v", out)
	}
}

func TestMathFloorCeilRound(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "math"; fn main() -> i32 { fmt.print(string(math.floor(2.7))); fmt.print(string(math.ceil(2.3))); fmt.print(string(math.round(2.5))); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "2" || out[1] != "3" || out[2] != "3" {
		t.Fatalf("got %v", out)
	}
}

func TestMathRequiresF64(t *testing.T) {
	_, _, err := evalBASL(`import "math"; fn main() -> i32 { math.sqrt(4); return 0; }`)
	if err == nil {
		t.Fatal("expected error: math.sqrt requires f64, not i32")
	}
}
