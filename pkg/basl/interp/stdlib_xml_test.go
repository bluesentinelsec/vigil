package interp

import "testing"

func TestXmlParseTagAndText(t *testing.T) {
	src := `import "fmt"; import "xml";
fn main() -> i32 {
	xml.Value root, err e = xml.parse("<book><title>Hello</title></book>");
	fmt.print(root.tag());
	array<xml.Value> kids = root.children();
	fmt.print(kids[0].tag());
	fmt.print(kids[0].text());
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "book" || out[1] != "title" || out[2] != "Hello" {
		t.Fatalf("got %v", out)
	}
}

func TestXmlAttr(t *testing.T) {
	src := `import "fmt"; import "xml";
fn main() -> i32 {
	xml.Value root, err e = xml.parse("<item id=\"42\" />");
	string v, bool found = root.attr("id");
	fmt.print(v);
	fmt.print(string(found));
	string v2, bool found2 = root.attr("missing");
	fmt.print(string(found2));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "42" || out[1] != "true" || out[2] != "false" {
		t.Fatalf("got %v", out)
	}
}

func TestXmlFindAndLen(t *testing.T) {
	src := `import "fmt"; import "xml";
fn main() -> i32 {
	xml.Value root, err e = xml.parse("<list><item>a</item><item>b</item><other>c</other></list>");
	fmt.print(string(root.len()));
	array<xml.Value> items = root.find("item");
	fmt.print(string(items.len()));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "3" || out[1] != "2" {
		t.Fatalf("got %v", out)
	}
}

func TestXmlFindOne(t *testing.T) {
	src := `import "fmt"; import "xml";
fn main() -> i32 {
	xml.Value root, err e = xml.parse("<a><b>yes</b></a>");
	xml.Value child, err e2 = root.find_one("b");
	fmt.print(child.text());
	fmt.print(string(e2));
	xml.Value missing, err e3 = root.find_one("z");
	fmt.print(string(e3));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "yes" || out[1] != "ok" {
		t.Fatalf("got %v", out)
	}
	if out[2] == "ok" {
		t.Fatal("expected error for missing element")
	}
}

func TestXmlParseInvalid(t *testing.T) {
	src := `import "fmt"; import "xml";
fn main() -> i32 {
	xml.Value root, err e = xml.parse("");
	fmt.print(string(e));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for empty XML")
	}
}
