//go:build !windows

// Package ffi provides dynamic loading of C shared libraries via dlopen/dlsym.
// This file is for macOS and Linux (cgo required).
package ffi

/*
#cgo !darwin LDFLAGS: -ldl
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// --- integer trampolines ---
typedef int (*fn_void_to_i32)();
typedef int (*fn_i32_to_i32)(int);
typedef int (*fn_i32_i32_to_i32)(int, int);
typedef void (*fn_i32_to_void)(int);
typedef void (*fn_void_to_void)();

static int call_void_to_i32(void* f) { return ((fn_void_to_i32)f)(); }
static int call_i32_to_i32(void* f, int a) { return ((fn_i32_to_i32)f)(a); }
static int call_i32_i32_to_i32(void* f, int a, int b) { return ((fn_i32_i32_to_i32)f)(a, b); }
static void call_i32_to_void(void* f, int a) { ((fn_i32_to_void)f)(a); }
static void call_void_to_void(void* f) { ((fn_void_to_void)f)(); }

// --- float trampolines ---
typedef double (*fn_void_to_f64)();
typedef double (*fn_f64_to_f64)(double);
typedef double (*fn_f64_f64_to_f64)(double, double);

static double call_void_to_f64(void* f) { return ((fn_void_to_f64)f)(); }
static double call_f64_to_f64(void* f, double a) { return ((fn_f64_to_f64)f)(a); }
static double call_f64_f64_to_f64(void* f, double a, double b) { return ((fn_f64_f64_to_f64)f)(a, b); }

// --- string trampolines ---
typedef const char* (*fn_void_to_str)();
typedef const char* (*fn_str_to_str)(const char*);
typedef void (*fn_str_to_void)(const char*);
typedef int (*fn_str_to_i32)(const char*);
typedef int (*fn_str_str_to_i32)(const char*, const char*);
typedef const char* (*fn_i32_to_str)(int);

static const char* call_void_to_str(void* f) { return ((fn_void_to_str)f)(); }
static const char* call_str_to_str(void* f, const char* a) { return ((fn_str_to_str)f)(a); }
static void call_str_to_void(void* f, const char* a) { ((fn_str_to_void)f)(a); }
static int call_str_to_i32(void* f, const char* a) { return ((fn_str_to_i32)f)(a); }
static int call_str_str_to_i32(void* f, const char* a, const char* b) { return ((fn_str_str_to_i32)f)(a, b); }
static const char* call_i32_to_str(void* f, int a) { return ((fn_i32_to_str)f)(a); }

// --- pointer trampolines ---
// Generic trampolines using void* args/returns for opaque pointer passing.
// Up to 6 void* args covers most C APIs (SDL3, etc.)
typedef void* (*fn_void_to_ptr)();
typedef void* (*fn_ptr_to_ptr)(void*);
typedef void* (*fn_ptr_ptr_to_ptr)(void*, void*);
typedef int (*fn_ptr_to_i32)(void*);
typedef void (*fn_ptr_to_void)(void*);
typedef int (*fn_ptr_i32_to_i32)(void*, int);
typedef void (*fn_ptr_i32_to_void)(void*, int);
typedef int (*fn_ptr_i32_i32_i32_i32_to_i32)(void*, int, int, int, int);

static void* call_void_to_ptr(void* f) { return ((fn_void_to_ptr)f)(); }
static void* call_ptr_to_ptr(void* f, void* a) { return ((fn_ptr_to_ptr)f)(a); }
static void* call_ptr_ptr_to_ptr(void* f, void* a, void* b) { return ((fn_ptr_ptr_to_ptr)f)(a, b); }
static int call_ptr_to_i32(void* f, void* a) { return ((fn_ptr_to_i32)f)(a); }
static void call_ptr_to_void(void* f, void* a) { ((fn_ptr_to_void)f)(a); }
static int call_ptr_i32_to_i32(void* f, void* a, int b) { return ((fn_ptr_i32_to_i32)f)(a, b); }
static void call_ptr_i32_to_void(void* f, void* a, int b) { ((fn_ptr_i32_to_void)f)(a, b); }
static int call_ptr_i32_i32_i32_i32_to_i32(void* f, void* a, int b, int c, int d, int e) { return ((fn_ptr_i32_i32_i32_i32_to_i32)f)(a, b, c, d, e); }

// Variadic trampoline: up to 6 void* args, returns void*
// This covers most SDL3 signatures when all args are cast to void*.
typedef void* (*fn_generic6)(void*, void*, void*, void*, void*, void*);
static void* call_generic(void* f, int nargs, void* a0, void* a1, void* a2, void* a3, void* a4, void* a5) {
    switch(nargs) {
    case 0: return ((void*(*)())f)();
    case 1: return ((void*(*)(void*))f)(a0);
    case 2: return ((void*(*)(void*,void*))f)(a0, a1);
    case 3: return ((void*(*)(void*,void*,void*))f)(a0, a1, a2);
    case 4: return ((void*(*)(void*,void*,void*,void*))f)(a0, a1, a2, a3);
    case 5: return ((void*(*)(void*,void*,void*,void*,void*))f)(a0, a1, a2, a3, a4);
    case 6: return ((void*(*)(void*,void*,void*,void*,void*,void*))f)(a0, a1, a2, a3, a4, a5);
    default: return (void*)0;
    }
}

// Helper to convert integer to void* on the C side, avoiding go vet warnings.
static void* to_ptr(uintptr_t v) { return (void*)v; }
*/
import "C"
import (
	"fmt"
	"unsafe"
)

// Lib represents a loaded shared library.
type Lib struct {
	handle unsafe.Pointer
	path   string
}

// BoundFunc represents a symbol bound with its C type signature.
type BoundFunc struct {
	ptr        unsafe.Pointer
	name       string
	retType    string
	paramTypes []string
}

// Policy controls which libraries can be loaded.
type Policy struct {
	AllowedDirs []string
	Allowlist   []string
	Enabled     bool
}

// Open loads a shared library. Returns error if policy denies it.
func Open(path string, policy *Policy) (*Lib, error) {
	if policy != nil && !policy.Enabled {
		return nil, fmt.Errorf("ffi: disabled by policy")
	}
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	handle := C.dlopen(cpath, C.RTLD_LAZY)
	if handle == nil {
		return nil, fmt.Errorf("ffi: dlopen %q: %s", path, C.GoString(C.dlerror()))
	}
	return &Lib{handle: handle, path: path}, nil
}

// Bind looks up a symbol and binds it with a declared type signature.
func (l *Lib) Bind(name, retType string, paramTypes []string) (*BoundFunc, error) {
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	ptr := C.dlsym(l.handle, cname)
	if ptr == nil {
		return nil, fmt.Errorf("ffi: dlsym %q: %s", name, C.GoString(C.dlerror()))
	}
	return &BoundFunc{ptr: ptr, name: name, retType: retType, paramTypes: paramTypes}, nil
}

// Close unloads the library.
func (l *Lib) Close() error {
	if l.handle != nil {
		C.dlclose(l.handle)
		l.handle = nil
	}
	return nil
}

// Signature returns a string key encoding the call signature for dispatch.
func signature(retType string, paramTypes []string) string {
	s := retType + "("
	for i, p := range paramTypes {
		if i > 0 {
			s += ","
		}
		s += p
	}
	return s + ")"
}

// Call invokes the bound function. Args are Go types: int32, float64, string.
// Returns a Go type matching retType, or nil for void.
func (f *BoundFunc) Call(args []interface{}) (interface{}, error) {
	if len(args) != len(f.paramTypes) {
		return nil, fmt.Errorf("ffi: %s: expected %d args, got %d", f.name, len(f.paramTypes), len(args))
	}

	sig := signature(f.retType, f.paramTypes)
	switch sig {
	// void return
	case "void()":
		C.call_void_to_void(f.ptr)
		return nil, nil
	case "void(i32)":
		a, err := asI32(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		C.call_i32_to_void(f.ptr, C.int(a))
		return nil, nil
	case "void(string)":
		a, err := asStr(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		ca := C.CString(a)
		defer C.free(unsafe.Pointer(ca))
		C.call_str_to_void(f.ptr, ca)
		return nil, nil

	// i32 return
	case "i32()":
		return int32(C.call_void_to_i32(f.ptr)), nil
	case "i32(i32)":
		a, err := asI32(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		return int32(C.call_i32_to_i32(f.ptr, C.int(a))), nil
	case "i32(i32,i32)":
		a, err := asI32(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		b, err := asI32(args[1], f.name, 1)
		if err != nil {
			return nil, err
		}
		return int32(C.call_i32_i32_to_i32(f.ptr, C.int(a), C.int(b))), nil
	case "i32(string)":
		a, err := asStr(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		ca := C.CString(a)
		defer C.free(unsafe.Pointer(ca))
		return int32(C.call_str_to_i32(f.ptr, ca)), nil
	case "i32(string,string)":
		a, err := asStr(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		b, err := asStr(args[1], f.name, 1)
		if err != nil {
			return nil, err
		}
		ca := C.CString(a)
		defer C.free(unsafe.Pointer(ca))
		cb := C.CString(b)
		defer C.free(unsafe.Pointer(cb))
		return int32(C.call_str_str_to_i32(f.ptr, ca, cb)), nil

	// f64 return
	case "f64()":
		return float64(C.call_void_to_f64(f.ptr)), nil
	case "f64(f64)":
		a, err := asF64(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		return float64(C.call_f64_to_f64(f.ptr, C.double(a))), nil
	case "f64(f64,f64)":
		a, err := asF64(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		b, err := asF64(args[1], f.name, 1)
		if err != nil {
			return nil, err
		}
		return float64(C.call_f64_f64_to_f64(f.ptr, C.double(a), C.double(b))), nil

	// string return
	case "string()":
		return C.GoString(C.call_void_to_str(f.ptr)), nil
	case "string(string)":
		a, err := asStr(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		ca := C.CString(a)
		defer C.free(unsafe.Pointer(ca))
		return C.GoString(C.call_str_to_str(f.ptr, ca)), nil
	case "string(i32)":
		a, err := asI32(args[0], f.name, 0)
		if err != nil {
			return nil, err
		}
		return C.GoString(C.call_i32_to_str(f.ptr, C.int(a))), nil
	}

	return nil, fmt.Errorf("ffi: %s: unsupported signature %s", f.name, sig)
}

// CallGeneric handles any signature involving ptr types via the generic C trampoline.
// All args are cast to uintptr (void*), result is void* cast back to uintptr.
func (f *BoundFunc) CallGeneric(args []interface{}) (uintptr, error) {
	if len(args) > 6 {
		return 0, fmt.Errorf("ffi: %s: generic call supports max 6 args", f.name)
	}
	var cargs [6]unsafe.Pointer
	for i, a := range args {
		switch v := a.(type) {
		case uintptr:
			cargs[i] = C.to_ptr(C.uintptr_t(v))
		case int32:
			cargs[i] = C.to_ptr(C.uintptr_t(v))
		case uint32:
			cargs[i] = C.to_ptr(C.uintptr_t(v))
		case string:
			cs := C.CString(v)
			defer C.free(unsafe.Pointer(cs))
			cargs[i] = unsafe.Pointer(cs)
		default:
			return 0, fmt.Errorf("ffi: %s: arg %d: unsupported type for generic call", f.name, i)
		}
	}
	result := C.call_generic(f.ptr, C.int(len(args)),
		cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5])
	return uintptr(result), nil
}

// RetType returns the declared return type.
func (f *BoundFunc) RetType() string { return f.retType }

// ParamTypes returns the declared parameter types.
func (f *BoundFunc) ParamTypes() []string { return f.paramTypes }

func asI32(v interface{}, name string, idx int) (int32, error) {
	if n, ok := v.(int32); ok {
		return n, nil
	}
	return 0, fmt.Errorf("ffi: %s: arg %d: expected i32", name, idx)
}

func asF64(v interface{}, name string, idx int) (float64, error) {
	if n, ok := v.(float64); ok {
		return n, nil
	}
	return 0, fmt.Errorf("ffi: %s: arg %d: expected f64", name, idx)
}

func asStr(v interface{}, name string, idx int) (string, error) {
	if s, ok := v.(string); ok {
		return s, nil
	}
	return "", fmt.Errorf("ffi: %s: arg %d: expected string", name, idx)
}
