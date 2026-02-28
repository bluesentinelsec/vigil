//go:build windows

// Package ffi provides dynamic loading of C shared libraries on Windows.
package ffi

/*
#cgo LDFLAGS: -lkernel32
#include <windows.h>
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

// --- pointer trampolines ---
typedef void* (*fn_ptr_to_ptr)(void*);
typedef void* (*fn_ptr_ptr_to_ptr)(void*, void*);
typedef void* (*fn_ptr_i32_to_ptr)(void*, int);
typedef void (*fn_ptr_to_void)(void*);
typedef void (*fn_ptr_ptr_to_void)(void*, void*);

static void* call_ptr_to_ptr(void* f, void* a) { return ((fn_ptr_to_ptr)f)(a); }
static void* call_ptr_ptr_to_ptr(void* f, void* a, void* b) { return ((fn_ptr_ptr_to_ptr)f)(a, b); }
static void* call_ptr_i32_to_ptr(void* f, void* a, int b) { return ((fn_ptr_i32_to_ptr)f)(a, b); }
static void call_ptr_to_void(void* f, void* a) { ((fn_ptr_to_void)f)(a); }
static void call_ptr_ptr_to_void(void* f, void* a, void* b) { ((fn_ptr_ptr_to_void)f)(a, b); }

// --- variadic trampolines ---
static void* call_variadic(void* f, int argc, void* a0, void* a1, void* a2, void* a3, void* a4, void* a5) {
    switch (argc) {
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

// Handle represents a loaded dynamic library.
type Handle struct {
	handle C.HMODULE
	path   string
}

// Lib represents a loaded shared library.
type Lib struct {
	handle C.HMODULE
	path   string
}

// BoundFunc represents a symbol bound with its C type signature.
type BoundFunc struct {
	ptr        unsafe.Pointer
	name       string
	RetType    string
	ParamTypes []string
}

// Policy controls which libraries can be loaded.
type Policy struct {
	AllowedDirs []string
	Allowlist   []string
	Enabled     bool
}

// Load opens a dynamic library at the given path (legacy API).
func Load(path string) (*Handle, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))

	h := C.LoadLibraryA(cpath)
	if h == nil {
		return nil, fmt.Errorf("LoadLibrary failed for %s", path)
	}

	return &Handle{handle: h, path: path}, nil
}

// Open loads a shared library. Returns error if policy denies it.
func Open(path string, policy *Policy) (*Lib, error) {
	if policy != nil && !policy.Enabled {
		return nil, fmt.Errorf("ffi: disabled by policy")
	}

	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))

	h := C.LoadLibraryA(cpath)
	if h == nil {
		return nil, fmt.Errorf("ffi: LoadLibrary %q failed", path)
	}

	return &Lib{handle: h, path: path}, nil
}

// Bind looks up a symbol and binds it with a declared type signature.
func (l *Lib) Bind(name, retType string, paramTypes []string) (*BoundFunc, error) {
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))

	ptr := C.GetProcAddress(l.handle, cname)
	if ptr == nil {
		return nil, fmt.Errorf("ffi: GetProcAddress %q failed", name)
	}

	return &BoundFunc{ptr: unsafe.Pointer(ptr), name: name, RetType: retType, ParamTypes: paramTypes}, nil
}

// Close unloads the library.
func (l *Lib) Close() error {
	if l.handle != nil {
		C.FreeLibrary(l.handle)
		l.handle = nil
	}
	return nil
}

// Close unloads the dynamic library.
func (h *Handle) Close() error {
	if h.handle != nil {
		C.FreeLibrary(h.handle)
		h.handle = nil
	}
	return nil
}

// Sym looks up a symbol in the loaded library.
func (h *Handle) Sym(name string) (unsafe.Pointer, error) {
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))

	sym := C.GetProcAddress(h.handle, cname)
	if sym == nil {
		return nil, fmt.Errorf("symbol %s not found in %s", name, h.path)
	}

	return unsafe.Pointer(sym), nil
}

// CallVoidToI32 calls a C function with signature: int fn()
func CallVoidToI32(fn unsafe.Pointer) int32 {
	return int32(C.call_void_to_i32(fn))
}

// CallI32ToI32 calls a C function with signature: int fn(int)
func CallI32ToI32(fn unsafe.Pointer, a int32) int32 {
	return int32(C.call_i32_to_i32(fn, C.int(a)))
}

// CallI32I32ToI32 calls a C function with signature: int fn(int, int)
func CallI32I32ToI32(fn unsafe.Pointer, a, b int32) int32 {
	return int32(C.call_i32_i32_to_i32(fn, C.int(a), C.int(b)))
}

// CallI32ToVoid calls a C function with signature: void fn(int)
func CallI32ToVoid(fn unsafe.Pointer, a int32) {
	C.call_i32_to_void(fn, C.int(a))
}

// CallVoidToVoid calls a C function with signature: void fn()
func CallVoidToVoid(fn unsafe.Pointer) {
	C.call_void_to_void(fn)
}

// CallPtrToPtr calls a C function with signature: void* fn(void*)
func CallPtrToPtr(fn, a unsafe.Pointer) unsafe.Pointer {
	return C.call_ptr_to_ptr(fn, a)
}

// CallPtrPtrToPtr calls a C function with signature: void* fn(void*, void*)
func CallPtrPtrToPtr(fn, a, b unsafe.Pointer) unsafe.Pointer {
	return C.call_ptr_ptr_to_ptr(fn, a, b)
}

// CallPtrI32ToPtr calls a C function with signature: void* fn(void*, int)
func CallPtrI32ToPtr(fn, a unsafe.Pointer, b int32) unsafe.Pointer {
	return C.call_ptr_i32_to_ptr(fn, a, C.int(b))
}

// CallPtrToVoid calls a C function with signature: void fn(void*)
func CallPtrToVoid(fn, a unsafe.Pointer) {
	C.call_ptr_to_void(fn, a)
}

// CallPtrPtrToVoid calls a C function with signature: void fn(void*, void*)
func CallPtrPtrToVoid(fn, a, b unsafe.Pointer) {
	C.call_ptr_ptr_to_void(fn, a, b)
}

// CallVariadic calls a C function with up to 6 pointer arguments.
func CallVariadic(fn unsafe.Pointer, args []unsafe.Pointer) unsafe.Pointer {
	argc := len(args)
	if argc > 6 {
		argc = 6
	}

	var a0, a1, a2, a3, a4, a5 unsafe.Pointer
	if argc > 0 {
		a0 = args[0]
	}
	if argc > 1 {
		a1 = args[1]
	}
	if argc > 2 {
		a2 = args[2]
	}
	if argc > 3 {
		a3 = args[3]
	}
	if argc > 4 {
		a4 = args[4]
	}
	if argc > 5 {
		a5 = args[5]
	}

	return C.call_variadic(fn, C.int(argc), a0, a1, a2, a3, a4, a5)
}

// IntToPtr converts an integer to a pointer.
func IntToPtr(v uintptr) unsafe.Pointer {
	return C.to_ptr(C.uintptr_t(v))
}

// Call invokes the bound function (simplified Windows implementation).
func (f *BoundFunc) Call(args []interface{}) (interface{}, error) {
	if len(args) != len(f.ParamTypes) {
		return nil, fmt.Errorf("ffi: %s: expected %d args, got %d", f.name, len(f.ParamTypes), len(args))
	}

	sig := f.RetType + "("
	for i, p := range f.ParamTypes {
		if i > 0 {
			sig += ","
		}
		sig += p
	}
	sig += ")"

	switch sig {
	case "void()":
		CallVoidToVoid(f.ptr)
		return nil, nil
	case "i32()":
		return CallVoidToI32(f.ptr), nil
	case "i32(i32)":
		a := args[0].(int32)
		return CallI32ToI32(f.ptr, a), nil
	case "i32(i32,i32)":
		a := args[0].(int32)
		b := args[1].(int32)
		return CallI32I32ToI32(f.ptr, a, b), nil
	case "void(i32)":
		a := args[0].(int32)
		CallI32ToVoid(f.ptr, a)
		return nil, nil
	default:
		return nil, fmt.Errorf("ffi: unsupported signature %s on Windows", sig)
	}
}

// CallGeneric invokes the bound function with variadic arguments (stub for Windows).
func (f *BoundFunc) CallGeneric(args ...interface{}) (interface{}, error) {
	return nil, fmt.Errorf("ffi: CallGeneric not yet implemented on Windows")
}
