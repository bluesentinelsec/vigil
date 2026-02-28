package interp

import (
	"path/filepath"
	"testing"
)

func TestSqliteOpenExecQuery(t *testing.T) {
	tmp := t.TempDir()
	db := filepath.Join(tmp, "test.db")
	src := `import "fmt"; import "sqlite";
fn main() -> i32 {
	SqliteDB db, err e1 = sqlite.open("` + db + `");
	err e2 = db.exec("CREATE TABLE t (id INTEGER, name TEXT)");
	err e3 = db.exec("INSERT INTO t VALUES (?, ?)", "1", "alice");
	SqliteRows rows, err e4 = db.query("SELECT id, name FROM t");
	bool has = rows.next();
	string id = rows.get("id");
	string name = rows.get("name");
	rows.close();
	db.close();
	fmt.print(string(e1));
	fmt.print(string(e2));
	fmt.print(string(e3));
	fmt.print(string(e4));
	fmt.print(string(has));
	fmt.print(id);
	fmt.print(name);
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ok" || out[1] != "ok" || out[2] != "ok" || out[3] != "ok" {
		t.Fatalf("errors: %v", out[:4])
	}
	if out[4] != "true" || out[5] != "1" || out[6] != "alice" {
		t.Fatalf("data: %v", out[4:])
	}
}

func TestSqliteInMemory(t *testing.T) {
	src := `import "fmt"; import "sqlite";
fn main() -> i32 {
	SqliteDB db, err e = sqlite.open(":memory:");
	db.exec("CREATE TABLE kv (k TEXT, v TEXT)");
	db.exec("INSERT INTO kv VALUES (?, ?)", "a", "1");
	db.exec("INSERT INTO kv VALUES (?, ?)", "b", "2");
	SqliteRows rows, err e2 = db.query("SELECT v FROM kv ORDER BY k");
	i32 count = 0;
	while (rows.next()) {
		fmt.print(rows.get("v"));
		count = count + 1;
	}
	rows.close();
	db.close();
	fmt.print(string(count));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "1" || out[1] != "2" || out[2] != "2" {
		t.Fatalf("got %v", out)
	}
}
