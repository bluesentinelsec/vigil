# sqlite

SQLite database access using pure-Go SQLite (`modernc.org/sqlite`). No native dependencies.

```
import "sqlite";
```

## Functions

### sqlite.open(string path) -> (SqliteDB, err)

Opens or creates a SQLite database file. Use `":memory:"` for an in-memory database.

- Returns `(db, ok)` on success.
- Returns `(void, err(message, err.io))` on failure.

```c
SqliteDB db, err e = sqlite.open("app.db");
SqliteDB mem, err e2 = sqlite.open(":memory:");
```

## SqliteDB Methods

### db.exec(string sql, ...params) -> err

Executes a SQL statement (INSERT, UPDATE, DELETE, CREATE, etc.). Parameters are positional (`?` placeholders).

- Parameters can be `string`, `i32`, `i64`, `f64`, or `bool`.
- Returns `ok` on success.
- Returns `err(message, err.io)` on failure.

```c
db.exec("CREATE TABLE users (id INTEGER, name TEXT)");
db.exec("INSERT INTO users VALUES (?, ?)", 1, "alice");
```

### db.query(string sql, ...params) -> (SqliteRows, err)

Executes a SELECT query and returns a row iterator.

- Returns `(rows, ok)` on success.
- Returns `(void, err(message, err.io))` on failure.

```c
SqliteRows rows, err e = db.query("SELECT name FROM users WHERE id = ?", 1);
```

### db.close() -> err

Closes the database connection.

## SqliteRows Methods

### rows.next() -> bool

Advances to the next row. Returns `true` if a row is available, `false` when exhausted.

### rows.get(string column_name) -> string

Returns the value of the named column in the current row as a string. All column types are returned as strings.

### rows.close() -> err

Closes the row iterator. Must be called when done iterating.

```c
SqliteDB db, err e = sqlite.open(":memory:");
db.exec("CREATE TABLE kv (k TEXT, v TEXT)");
db.exec("INSERT INTO kv VALUES (?, ?)", "a", "1");
db.exec("INSERT INTO kv VALUES (?, ?)", "b", "2");

SqliteRows rows, err e2 = db.query("SELECT k, v FROM kv ORDER BY k");
while (rows.next()) {
    fmt.println(fmt.sprintf("%s = %s", rows.get("k"), rows.get("v")));
}
rows.close();
db.close();
// Output:
// a = 1
// b = 2
```
