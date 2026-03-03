# regex

Regular expression matching, searching, and replacement. Patterns use Go's RE2 syntax.

```
import "regex";
```

## Functions

All functions return an `err` component. On invalid regex pattern, the error contains the parse failure message.

### regex.match(string pattern, string s) -> (bool, err)

Tests whether `s` matches `pattern`.

- Returns `(true/false, ok)` on valid pattern.
- Returns `(false, err(message, err.parse))` on invalid pattern.

```c
bool m, err e = regex.match("^hello", "hello world");  // m = true
bool n, err e2 = regex.match("^world", "hello world"); // n = false
```

### regex.find(string pattern, string s) -> (string, err)

Returns the first match of `pattern` in `s`, or `""` if no match.

- Returns `(match, ok)` on valid pattern.
- Returns `("", err(message, err.parse))` on invalid pattern.

```c
string m, err e = regex.find("[0-9]+", "abc123def");  // m = "123"
```

### regex.find_all(string pattern, string s) -> (array\<string\>, err)

Returns all non-overlapping matches of `pattern` in `s`.

- Returns `(matches, ok)` on valid pattern.
- Returns `([], err(message, err.parse))` on invalid pattern.

```c
array<string> m, err e = regex.find_all("[0-9]+", "a1b2c3");
// m = ["1", "2", "3"]
```

### regex.replace(string pattern, string s, string repl) -> (string, err)

Replaces all matches of `pattern` in `s` with `repl`.

- Returns `(result, ok)` on valid pattern.
- Returns `("", err(message, err.parse))` on invalid pattern.

```c
string r, err e = regex.replace("[0-9]", "a1b2c3", "X");
// r = "aXbXcX"
```

### regex.split(string pattern, string s) -> (array\<string\>, err)

Splits `s` by all matches of `pattern`.

- Returns `(parts, ok)` on valid pattern.
- Returns `([], err(message, err.parse))` on invalid pattern.

```c
array<string> parts, err e = regex.split("[,;]", "a,b;c");
// parts = ["a", "b", "c"]
```
