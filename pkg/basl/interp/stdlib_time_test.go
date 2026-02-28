package interp

import "testing"

func TestTimeNow(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "time"; fn main() -> i32 { i64 ts = time.now(); fmt.print(string(ts > i64(0))); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" {
		t.Fatalf("got %q", out[0])
	}
}

func TestTimeFormat(t *testing.T) {
	// Verify time.format returns a non-empty string for a known timestamp
	_, out, err := evalBASL(`import "fmt"; import "time"; fn main() -> i32 { string s = time.format(i64(946684800000), "2006-01-02"); fmt.print(string(s.len() > 0)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" {
		t.Fatalf("got %q", out[0])
	}
}

func TestTimeParse(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "time"; fn main() -> i32 { i64 ms, err e = time.parse("2006-01-02", "2000-01-01"); fmt.print(string(e)); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ok" {
		t.Fatalf("got %q", out[0])
	}
}
