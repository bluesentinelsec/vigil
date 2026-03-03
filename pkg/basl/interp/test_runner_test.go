package interp

import "testing"

func TestTestRunnerPass(t *testing.T) {
	src := []byte(`import "test";
fn test_one(test.T t) -> void { t.assert(1 == 1, "should pass"); }
fn test_two(test.T t) -> void { t.assert(true, "also pass"); }
`)
	results, err := ExecTestFile("fake_test.basl", src, "", nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(results) != 2 {
		t.Fatalf("expected 2 results, got %d", len(results))
	}
	for _, r := range results {
		if !r.Passed {
			t.Fatalf("%s failed: %s", r.Name, r.Message)
		}
	}
}

func TestTestRunnerFail(t *testing.T) {
	src := []byte(`import "test";
fn test_bad(test.T t) -> void { t.assert(false, "nope"); }
`)
	results, err := ExecTestFile("fake_test.basl", src, "", nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 || results[0].Passed {
		t.Fatalf("expected failure, got %v", results)
	}
	if results[0].Message != "nope" {
		t.Fatalf("expected message 'nope', got %q", results[0].Message)
	}
}

func TestTestRunnerTFail(t *testing.T) {
	src := []byte(`import "test";
fn test_explicit(test.T t) -> void { t.fail("explicit fail"); }
`)
	results, err := ExecTestFile("fake_test.basl", src, "", nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 || results[0].Passed {
		t.Fatalf("expected failure, got %v", results)
	}
	if results[0].Message != "explicit fail" {
		t.Fatalf("got %q", results[0].Message)
	}
}

func TestTestRunnerFilter(t *testing.T) {
	src := []byte(`import "test";
fn test_alpha(test.T t) -> void { t.assert(true, "ok"); }
fn test_beta(test.T t) -> void { t.assert(true, "ok"); }
fn test_gamma(test.T t) -> void { t.assert(true, "ok"); }
`)
	results, err := ExecTestFile("fake_test.basl", src, "alpha|gamma", nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(results) != 2 {
		t.Fatalf("expected 2 filtered results, got %d", len(results))
	}
	if results[0].Name != "test_alpha" || results[1].Name != "test_gamma" {
		t.Fatalf("unexpected names: %s, %s", results[0].Name, results[1].Name)
	}
}

func TestTestRunnerSkipsNonTestFuncs(t *testing.T) {
	src := []byte(`import "test";
fn helper() -> i32 { return 42; }
fn test_uses_helper(test.T t) -> void { t.assert(helper() == 42, "helper works"); }
`)
	results, err := ExecTestFile("fake_test.basl", src, "", nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 {
		t.Fatalf("expected 1 test, got %d", len(results))
	}
	if !results[0].Passed {
		t.Fatalf("test failed: %s", results[0].Message)
	}
}

func TestTestRunnerIsolation(t *testing.T) {
	// Each test gets its own interpreter — global state doesn't leak.
	src := []byte(`import "t";
i32 counter = 0;
fn test_first() -> void {
	counter = counter + 1;
	t.assert(counter == 1, "counter should be 1");
}
fn test_second() -> void {
	counter = counter + 1;
	t.assert(counter == 1, "counter should be 1 (isolated)");
}
`)
	results, err := ExecTestFile("fake_test.basl", src, "", nil)
	if err != nil {
		t.Fatal(err)
	}
	for _, r := range results {
		if !r.Passed {
			t.Fatalf("%s failed: %s", r.Name, r.Message)
		}
	}
}
