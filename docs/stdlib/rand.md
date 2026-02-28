# rand

Cryptographically secure random number generation. Uses `crypto/rand` internally.

```
import "rand";
```

## Functions

### rand.bytes(i32 count) -> string

Generates `count` cryptographically secure random bytes and returns them as a lowercase hex string. The returned string is `count * 2` characters long.

- Error if arg is not `i32`: `"rand.bytes: expected i32 count"`.

```c
string key = rand.bytes(32);  // 64 hex chars, suitable as AES-256 key
string nonce = rand.bytes(16); // 32 hex chars
```

### rand.int(i32 min, i32 max) -> i32

Returns a cryptographically secure random integer in the range `[min, max)` (inclusive min, exclusive max).

- Error if args are not `(i32, i32)`: `"rand.int: expected (i32 min, i32 max)"`.

```c
i32 n = rand.int(0, 100);   // 0 <= n < 100
i32 d = rand.int(1, 7);     // 1 <= d < 7 (dice roll)
```
