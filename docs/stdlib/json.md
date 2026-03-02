# json

JSON parsing and serialization. Parsed values are wrapped in `json.Value` objects with typed accessor methods.

```
import "json";
```

## Functions

### json.parse(string s) -> (json.Value, err)

Parses a JSON string into a `json.Value`.

- Returns `(value, ok)` on success.
- Returns `(void, err(message, err.parse))` on invalid JSON.

```c
json.Value v, err e = json.parse("{\"name\":\"alice\",\"age\":30}");
```

### json.stringify(val) -> string

Serializes a value to a compact JSON string (no indentation).

- Accepts both `json.Value` objects and native BASL values (strings, numbers, bools, arrays, maps).
- Native BASL values are converted to Go types before marshaling.

```c
string s = json.stringify(v);  // "{\"name\":\"alice\",\"age\":30}"
```

## json.Value Methods

### v.get_string(string key) -> string

Returns the string value for `key`. Returns `""` if the key is missing or the value is not a string.

### v.get_i32(string key) -> i32

Returns the integer value for `key`. Returns `0` if the key is missing or the value is not a number.

### v.get_f64(string key) -> f64

Returns the float value for `key`. Returns `0.0` if the key is missing or the value is not a number.

### v.get_bool(string key) -> bool

Returns the boolean value for `key`. Returns `false` if the key is missing or the value is not a boolean.

```c
json.Value v, err e = json.parse("{\"name\":\"alice\",\"age\":30,\"active\":true}");
string name = v.get_string("name");   // "alice"
i32 age = v.get_i32("age");           // 30
bool active = v.get_bool("active");   // true
string missing = v.get_string("nope"); // ""
```

### v.get(string key) -> (json.Value, err)

Returns a nested `json.Value` for the given key.

- Returns `(value, ok)` if the key exists.
- Returns `(void, err("key not found: KEY", err.not_found))` if missing.

```c
json.Value inner, err e = v.get("address");
```

### v.at(i32 index) -> (json.Value, err)

Accesses an element of a JSON array by index. Bounds-checked.

- Returns `(value, ok)` on success.
- Returns `(void, err("index out of range", err.bounds))` if out of bounds.

### v.at_i32(i32 index) -> i32

Returns the integer at the given array index. Returns `0` if out of bounds or not a number.

### v.at_string(i32 index) -> string

Returns the string at the given array index. Returns `""` if out of bounds or not a string.

### v.len() -> i32

Returns the length of a JSON array, or `0` if the value is not an array.

### v.keys() -> array\<string\>

Returns the keys of a JSON object as an array. Returns `[]` if the value is not an object.

```c
json.Value arr, err e = json.parse("[1, 2, 3]");
i32 length = arr.len();           // 3
i32 first = arr.at_i32(0);        // 1
json.Value el, err e2 = arr.at(1); // json.Value wrapping 2
```
