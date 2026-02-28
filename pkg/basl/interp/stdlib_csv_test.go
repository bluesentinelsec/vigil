package interp

import "testing"

func TestCsvParseStringify(t *testing.T) {
	src := `import "fmt"; import "csv";
fn main() -> i32 {
	array<array<string>> rows, err e = csv.parse("a,b\n1,2\n");
	fmt.print(string(rows.len()));
	fmt.print(rows[0][0]);
	fmt.print(rows[1][1]);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "2" || out[1] != "a" || out[2] != "2" {
		t.Fatalf("got %v", out)
	}
}
