package interp

import "testing"

func TestSortInts(t *testing.T) {
	src := `import "fmt"; import "sort";
fn main() -> i32 {
	array<i32> a = [3, 1, 2];
	sort.ints(a);
	fmt.print(string(a[0]));
	fmt.print(string(a[1]));
	fmt.print(string(a[2]));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "1" || out[1] != "2" || out[2] != "3" {
		t.Fatalf("got %v", out)
	}
}

func TestSortStrings(t *testing.T) {
	src := `import "fmt"; import "sort";
fn main() -> i32 {
	array<string> a = ["c", "a", "b"];
	sort.strings(a);
	fmt.print(a[0]);
	fmt.print(a[1]);
	fmt.print(a[2]);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "a" || out[1] != "b" || out[2] != "c" {
		t.Fatalf("got %v", out)
	}
}
