# hash

Cryptographic hash functions. All return lowercase hex-encoded strings.

```
import "hash";
```

## Functions

### hash.sha256(string data) -> string

Returns the SHA-256 hash as a 64-character hex string.

```c
string h = hash.sha256("hello");
// "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
```

### hash.sha512(string data) -> string

Returns the SHA-512 hash as a 128-character hex string.

### hash.sha1(string data) -> string

Returns the SHA-1 hash as a 40-character hex string.

### hash.md5(string data) -> string

Returns the MD5 hash as a 32-character hex string.

```c
string h = hash.md5("hello");
// "5d41402abc4b2a76b9719d911017c592"
```

### hash.hmac_sha256(string key, string data) -> string

Returns the HMAC-SHA256 as a 64-character hex string.

```c
string h = hash.hmac_sha256("secret", "message");
```
