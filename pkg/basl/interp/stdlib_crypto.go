package interp

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"io"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeCryptoModule() *Env {
	env := NewEnv(nil)

	// AES-GCM encrypt: crypto.aes_encrypt(key_hex, plaintext) -> (ciphertext_hex, err)
	env.Define("aes_encrypt", value.NewNativeFunc("crypto.aes_encrypt", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("crypto.aes_encrypt: expected (string key_hex, string plaintext)")
		}
		key, err := hex.DecodeString(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("bad key hex: "+err.Error(), value.ErrKindArg)}}
		}
		block, err := aes.NewCipher(key)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		gcm, err := cipher.NewGCM(block)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		nonce := make([]byte, gcm.NonceSize())
		if _, err := io.ReadFull(rand.Reader, nonce); err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		ct := gcm.Seal(nonce, nonce, []byte(args[1].AsString()), nil)
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(hex.EncodeToString(ct)), value.Ok}}
	}))

	// AES-GCM decrypt: crypto.aes_decrypt(key_hex, ciphertext_hex) -> (plaintext, err)
	env.Define("aes_decrypt", value.NewNativeFunc("crypto.aes_decrypt", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("crypto.aes_decrypt: expected (string key_hex, string ciphertext_hex)")
		}
		key, err := hex.DecodeString(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("bad key hex: "+err.Error(), value.ErrKindArg)}}
		}
		ct, err := hex.DecodeString(args[1].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("bad ciphertext hex: "+err.Error(), value.ErrKindArg)}}
		}
		block, err := aes.NewCipher(key)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		gcm, err := cipher.NewGCM(block)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		ns := gcm.NonceSize()
		if len(ct) < ns {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("ciphertext too short", value.ErrKindArg)}}
		}
		pt, err := gcm.Open(nil, ct[:ns], ct[ns:], nil)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(pt)), value.Ok}}
	}))

	// RSA key generation: crypto.rsa_generate(bits) -> (priv_pem, pub_pem, err)
	env.Define("rsa_generate", value.NewNativeFunc("crypto.rsa_generate", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("crypto.rsa_generate: expected i32 bits")
		}
		bits := int(args[0].AsI32())
		priv, err := rsa.GenerateKey(rand.Reader, bits)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		privPem := pem.EncodeToMemory(&pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(priv)})
		pubBytes, err := x509.MarshalPKIXPublicKey(&priv.PublicKey)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		pubPem := pem.EncodeToMemory(&pem.Block{Type: "PUBLIC KEY", Bytes: pubBytes})
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(privPem)), value.NewString(string(pubPem)), value.Ok}}
	}))

	// RSA encrypt: crypto.rsa_encrypt(pub_pem, plaintext) -> (ciphertext_hex, err)
	env.Define("rsa_encrypt", value.NewNativeFunc("crypto.rsa_encrypt", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("crypto.rsa_encrypt: expected (string pub_pem, string plaintext)")
		}
		pub, err := parseRSAPublicKey(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		ct, err := rsa.EncryptOAEP(sha256.New(), rand.Reader, pub, []byte(args[1].AsString()), nil)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(hex.EncodeToString(ct)), value.Ok}}
	}))

	// RSA decrypt: crypto.rsa_decrypt(priv_pem, ciphertext_hex) -> (plaintext, err)
	env.Define("rsa_decrypt", value.NewNativeFunc("crypto.rsa_decrypt", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("crypto.rsa_decrypt: expected (string priv_pem, string ciphertext_hex)")
		}
		priv, err := parseRSAPrivateKey(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		ct, err := hex.DecodeString(args[1].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("bad hex: "+err.Error(), value.ErrKindArg)}}
		}
		pt, err := rsa.DecryptOAEP(sha256.New(), rand.Reader, priv, ct, nil)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(pt)), value.Ok}}
	}))

	// RSA sign: crypto.rsa_sign(priv_pem, data) -> (sig_hex, err)
	env.Define("rsa_sign", value.NewNativeFunc("crypto.rsa_sign", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("crypto.rsa_sign: expected (string priv_pem, string data)")
		}
		priv, err := parseRSAPrivateKey(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindArg)}}
		}
		h := sha256.Sum256([]byte(args[1].AsString()))
		sig, err := rsa.SignPKCS1v15(rand.Reader, priv, 0, h[:])
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(hex.EncodeToString(sig)), value.Ok}}
	}))

	// RSA verify: crypto.rsa_verify(pub_pem, data, sig_hex) -> bool
	env.Define("rsa_verify", value.NewNativeFunc("crypto.rsa_verify", func(args []value.Value) (value.Value, error) {
		if len(args) != 3 || args[0].T != value.TypeString || args[1].T != value.TypeString || args[2].T != value.TypeString {
			return value.Void, fmt.Errorf("crypto.rsa_verify: expected (string pub_pem, string data, string sig_hex)")
		}
		pub, err := parseRSAPublicKey(args[0].AsString())
		if err != nil {
			return value.NewBool(false), nil
		}
		sig, err := hex.DecodeString(args[2].AsString())
		if err != nil {
			return value.NewBool(false), nil
		}
		h := sha256.Sum256([]byte(args[1].AsString()))
		err = rsa.VerifyPKCS1v15(pub, 0, h[:], sig)
		return value.NewBool(err == nil), nil
	}))

	return env
}

func parseRSAPrivateKey(pemStr string) (*rsa.PrivateKey, error) {
	block, _ := pem.Decode([]byte(pemStr))
	if block == nil {
		return nil, fmt.Errorf("failed to decode PEM")
	}
	return x509.ParsePKCS1PrivateKey(block.Bytes)
}

func parseRSAPublicKey(pemStr string) (*rsa.PublicKey, error) {
	block, _ := pem.Decode([]byte(pemStr))
	if block == nil {
		return nil, fmt.Errorf("failed to decode PEM")
	}
	pub, err := x509.ParsePKIXPublicKey(block.Bytes)
	if err != nil {
		return nil, err
	}
	rsaPub, ok := pub.(*rsa.PublicKey)
	if !ok {
		return nil, fmt.Errorf("not an RSA public key")
	}
	return rsaPub, nil
}

// ── rand module ──
