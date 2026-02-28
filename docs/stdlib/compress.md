# compress

Gzip and zlib compression/decompression. Operates on string data.

```
import "compress";
```

## Functions

### compress.gzip(string data) -> (string, err)

Compresses data using gzip. The result is a binary string (not hex-encoded).

- Returns `(compressed, ok)` on success.
- Returns `("", err(message))` on failure.

### compress.gunzip(string data) -> (string, err)

Decompresses gzip data.

- Returns `(decompressed, ok)` on success.
- Returns `("", err(message))` on invalid gzip data.

```c
string compressed, err e1 = compress.gzip("hello world");
string original, err e2 = compress.gunzip(compressed);
// original = "hello world"
```

### compress.zlib(string data) -> (string, err)

Compresses data using zlib. The result is a binary string.

- Returns `(compressed, ok)` on success.
- Returns `("", err(message))` on failure.

### compress.unzlib(string data) -> (string, err)

Decompresses zlib data.

- Returns `(decompressed, ok)` on success.
- Returns `("", err(message))` on invalid zlib data.

```c
string compressed, err e1 = compress.zlib("test data");
string original, err e2 = compress.unzlib(compressed);
// original = "test data"
```
