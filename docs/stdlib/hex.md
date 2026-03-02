# hex

Hexadecimal encoding and decoding.

```
import "hex";
```

## Functions

### hex.encode(string data) -> string

Encodes a string to lowercase hexadecimal. Always succeeds.

```c
string h = hex.encode("AB");  // "4142"
```

### hex.decode(string encoded) -> (string, err)

Decodes a hexadecimal string.

- Returns `(decoded, ok)` on success.
- Returns `("", err(message, err.parse))` on invalid hex input (odd length, non-hex chars).

```c
string decoded, err e = hex.decode("4142");  // decoded = "AB"
```
