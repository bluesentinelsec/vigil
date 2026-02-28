package interp

import "testing"

func TestHexEncodeDecode(t *testing.T) {
	src := `import "fmt"; import "hex";
fn main() -> i32 {
	string encoded = hex.encode("AB");
	fmt.print(encoded);
	string decoded, err e = hex.decode(encoded);
	fmt.print(decoded);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "4142" || out[1] != "AB" {
		t.Fatalf("got %v", out)
	}
}
