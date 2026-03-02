# crypto

Cryptographic operations: AES-256-GCM symmetric encryption and RSA asymmetric encryption/signing.

```
import "crypto";
```

## AES-256-GCM

### crypto.aes_encrypt(string key_hex, string plaintext) -> (string, err)

Encrypts plaintext using AES-256-GCM.

- `key_hex`: 64-character hex string (32 bytes = AES-256).
- A random 12-byte nonce is generated and prepended to the ciphertext.
- Returns `(ciphertext_hex, ok)` on success.
- Returns `("", err(message, err.arg))` on failure (bad key hex, wrong key length).

```c
string key = rand.bytes(32);  // 64 hex chars
string ct, err e = crypto.aes_encrypt(key, "secret message");
```

### crypto.aes_decrypt(string key_hex, string ciphertext_hex) -> (string, err)

Decrypts AES-256-GCM ciphertext.

- Extracts the nonce from the first 12 bytes of the decoded ciphertext.
- Returns `(plaintext, ok)` on success.
- Returns `("", err(message, err.arg))` on failure (bad hex, wrong key, tampered data, ciphertext too short).

```c
string pt, err e = crypto.aes_decrypt(key, ct);  // "secret message"
```

## RSA

### crypto.rsa_generate(i32 bits) -> (string, string, err)

Generates an RSA key pair.

- Returns `(private_pem, public_pem, ok)` on success.
- Returns `("", "", err(message, err.io))` on failure.
- Private key: PKCS#1 PEM format (`RSA PRIVATE KEY`).
- Public key: PKIX PEM format (`PUBLIC KEY`).

```c
string priv, string pubkey, err e = crypto.rsa_generate(2048);
```

### crypto.rsa_encrypt(string pub_pem, string plaintext) -> (string, err)

Encrypts plaintext using RSA-OAEP with SHA-256.

- Returns `(ciphertext_hex, ok)` on success.
- Returns `("", err(message, err.arg))` on failure.

### crypto.rsa_decrypt(string priv_pem, string ciphertext_hex) -> (string, err)

Decrypts RSA-OAEP ciphertext.

- Returns `(plaintext, ok)` on success.
- Returns `("", err(message, err.arg))` on failure.

```c
string ct, err e1 = crypto.rsa_encrypt(pubkey, "secret");
string pt, err e2 = crypto.rsa_decrypt(priv, ct);  // "secret"
```

### crypto.rsa_sign(string priv_pem, string data) -> (string, err)

Signs data using PKCS#1 v1.5 with SHA-256.

- Returns `(signature_hex, ok)` on success.
- Returns `("", err(message, err.arg))` on failure.

### crypto.rsa_verify(string pub_pem, string data, string sig_hex) -> bool

Verifies a PKCS#1 v1.5 SHA-256 signature.

- Returns `true` if the signature is valid.
- Returns `false` if invalid, or if any input is malformed.

```c
string sig, err e = crypto.rsa_sign(priv, "hello");
bool valid = crypto.rsa_verify(pubkey, "hello", sig);      // true
bool invalid = crypto.rsa_verify(pubkey, "tampered", sig);  // false
```
