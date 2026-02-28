package interp

import "testing"

func TestLogSetLevel(t *testing.T) {
	// set_level to "error" should suppress info
	_, out, err := evalBASL(`import "fmt"; import "log"; fn main() -> i32 { log.set_level("error"); log.info("suppressed"); fmt.print("done"); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) != 1 || out[0] != "done" {
		t.Fatalf("got %v", out)
	}
	// reset for other tests
	logLevel = 1
}

func TestLogSetHandler(t *testing.T) {
	_, out, err := evalBASL(`import "fmt"; import "log"; fn handler(string level, string msg) { fmt.print(level); fmt.print(msg); } fn main() -> i32 { log.set_level("debug"); log.set_handler(handler); log.info("hi"); return 0; }`)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) != 2 || out[0] != "INFO" || out[1] != "hi" {
		t.Fatalf("got %v", out)
	}
	logLevel = 1
}

func TestLogInvalidLevel(t *testing.T) {
	_, _, err := evalBASL(`import "log"; fn main() -> i32 { log.set_level("bogus"); return 0; }`)
	if err == nil {
		t.Fatal("expected error for unknown log level")
	}
}
