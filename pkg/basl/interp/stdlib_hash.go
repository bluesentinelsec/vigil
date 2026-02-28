package interp

import (
	"crypto/hmac"
	"crypto/md5"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/hex"
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ── hash module ──

func (interp *Interpreter) makeHashModule() *Env {
	env := NewEnv(nil)

	hashFn := func(name string, sumFn func([]byte) string) {
		env.Define(name, value.NewNativeFunc("hash."+name, func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("hash.%s: expected string", name)
			}
			return value.NewString(sumFn([]byte(args[0].AsString()))), nil
		}))
	}

	hashFn("md5", func(b []byte) string { s := md5.Sum(b); return hex.EncodeToString(s[:]) })
	hashFn("sha1", func(b []byte) string { s := sha1.Sum(b); return hex.EncodeToString(s[:]) })
	hashFn("sha256", func(b []byte) string { s := sha256.Sum256(b); return hex.EncodeToString(s[:]) })
	hashFn("sha512", func(b []byte) string { s := sha512.Sum512(b); return hex.EncodeToString(s[:]) })

	env.Define("hmac_sha256", value.NewNativeFunc("hash.hmac_sha256", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("hash.hmac_sha256: expected (string key, string data)")
		}
		mac := hmac.New(sha256.New, []byte(args[0].AsString()))
		mac.Write([]byte(args[1].AsString()))
		return value.NewString(hex.EncodeToString(mac.Sum(nil))), nil
	}))

	return env
}

// ── crypto module ──
