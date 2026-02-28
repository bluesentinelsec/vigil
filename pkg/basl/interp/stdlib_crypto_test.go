package interp

import "testing"

func TestCryptoAesRoundtrip(t *testing.T) {
	// 32-byte key = 64 hex chars for AES-256
	src := `import "fmt"; import "crypto"; import "rand";
fn main() -> i32 {
	string key = rand.bytes(32);
	string ct, err e1 = crypto.aes_encrypt(key, "secret message");
	string pt, err e2 = crypto.aes_decrypt(key, ct);
	fmt.print(pt);
	fmt.print(string(e1));
	fmt.print(string(e2));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "secret message" || out[1] != "ok" || out[2] != "ok" {
		t.Fatalf("got %v", out)
	}
}

func TestCryptoAesBadKey(t *testing.T) {
	src := `import "fmt"; import "crypto";
fn main() -> i32 {
	string ct, err e = crypto.aes_encrypt("badkey", "data");
	fmt.print(string(e));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for bad key")
	}
}

func TestCryptoRsaSignVerify(t *testing.T) {
	src := `import "fmt"; import "crypto";
fn main() -> i32 {
	string priv, string pubkey, err e1 = crypto.rsa_generate(2048);
	string sig, err e2 = crypto.rsa_sign(priv, "hello");
	bool valid = crypto.rsa_verify(pubkey, "hello", sig);
	bool invalid = crypto.rsa_verify(pubkey, "tampered", sig);
	fmt.print(string(e1));
	fmt.print(string(e2));
	fmt.print(string(valid));
	fmt.print(string(invalid));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ok" || out[1] != "ok" || out[2] != "true" || out[3] != "false" {
		t.Fatalf("got %v", out)
	}
}

func TestCryptoRsaEncryptDecrypt(t *testing.T) {
	src := `import "fmt"; import "crypto";
fn main() -> i32 {
	string priv, string pubkey, err e1 = crypto.rsa_generate(2048);
	string ct, err e2 = crypto.rsa_encrypt(pubkey, "secret");
	string pt, err e3 = crypto.rsa_decrypt(priv, ct);
	fmt.print(pt);
	fmt.print(string(e2));
	fmt.print(string(e3));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "secret" || out[1] != "ok" || out[2] != "ok" {
		t.Fatalf("got %v", out)
	}
}
