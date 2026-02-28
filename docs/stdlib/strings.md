# strings

String utility functions beyond the built-in string methods.

```
import "strings";
```

## Functions

### strings.join(array\<string\> parts, string sep) -> string

Joins array elements into a single string with the given separator.

- Error if args are not `(array<string>, string)`: `"strings.join: expected (array<string>, string sep)"`.

```c
array<string> a = ["x", "y", "z"];
string s = strings.join(a, ",");  // "x,y,z"
string t = strings.join(a, "");   // "xyz"
```

### strings.repeat(string s, i32 count) -> string

Repeats the string `count` times.

- Error if args are not `(string, i32)`: `"strings.repeat: expected (string, i32)"`.

```c
string s = strings.repeat("ab", 3);  // "ababab"
string t = strings.repeat("-", 10);   // "----------"
```
