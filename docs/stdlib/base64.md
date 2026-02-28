# base64

Base64 encoding and decoding (standard encoding with padding).

```
import "base64";
```

## Functions

### base64.encode(string data) -> string

Encodes a string to base64. Always succeeds.

```c
string encoded = base64.encode("hello world");  // "aGVsbG8gd29ybGQ="
```

### base64.decode(string encoded) -> (string, err)

Decodes a base64 string.

- Returns `(decoded, ok)` on success.
- Returns `("", err(message))` on invalid base64 input.

```c
string decoded, err e = base64.decode("aGVsbG8gd29ybGQ=");
// decoded = "hello world"
```
