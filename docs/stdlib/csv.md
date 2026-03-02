# csv

CSV parsing and serialization.

```
import "csv";
```

## Functions

### csv.parse(string data) -> (array\<array\<string\>\>, err)

Parses a CSV string into a 2D array of strings. Each inner array is one row.

- Returns `(rows, ok)` on success.
- Returns `([], err(message, err.parse))` on malformed CSV.

```c
array<array<string>> rows, err e = csv.parse("a,b\n1,2\n");
// rows = [["a","b"], ["1","2"]]
string cell = rows[0][0];  // "a"
```

### csv.stringify(array\<array\<string\>\> rows) -> (string, err)

Serializes a 2D array of strings into a CSV string.

- Returns `(csv_string, ok)` on success.
- Each inner array must contain only strings.

```c
array<array<string>> data = [["name","age"], ["alice","30"]];
string out, err e = csv.stringify(data);
// out = "name,age\nalice,30\n"
```
