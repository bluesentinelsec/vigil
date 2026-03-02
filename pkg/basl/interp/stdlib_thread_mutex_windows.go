//go:build windows

package interp

/*
#include <windows.h>
#include <stdint.h>

typedef intptr_t (*basl_cb_fn)(intptr_t, intptr_t, intptr_t, intptr_t);

struct thread_ctx {
    basl_cb_fn fn;
    intptr_t   arg;
};

static DWORD WINAPI thread_entry(LPVOID raw) {
    struct thread_ctx* ctx = (struct thread_ctx*)raw;
    basl_cb_fn fn = ctx->fn;
    intptr_t arg = ctx->arg;
    free(ctx);
    fn(arg, 0, 0, 0);
    return 0;
}

static int basl_thread_create(HANDLE* tid, basl_cb_fn fn, intptr_t arg) {
    struct thread_ctx* ctx = (struct thread_ctx*)malloc(sizeof(struct thread_ctx));
    if (!ctx) return 1;
    ctx->fn = fn;
    ctx->arg = arg;
    *tid = CreateThread(NULL, 0, thread_entry, ctx, 0, NULL);
    if (*tid == NULL) {
        free(ctx);
        return 1;
    }
    return 0;
}

static int basl_thread_join(HANDLE tid) {
    DWORD result = WaitForSingleObject(tid, INFINITE);
    CloseHandle(tid);
    return (result == WAIT_OBJECT_0) ? 0 : 1;
}

static int basl_mutex_init(CRITICAL_SECTION* m) {
    InitializeCriticalSection(m);
    return 0;
}

static int basl_mutex_lock(CRITICAL_SECTION* m) {
    EnterCriticalSection(m);
    return 0;
}

static int basl_mutex_unlock(CRITICAL_SECTION* m) {
    LeaveCriticalSection(m);
    return 0;
}

static int basl_mutex_destroy(CRITICAL_SECTION* m) {
    DeleteCriticalSection(m);
    return 0;
}

static void basl_sleep_ms(int ms) {
    Sleep(ms);
}
*/
import "C"
import (
	"fmt"
	"sync"
	"unsafe"

	"github.com/bluesentinelsec/basl/pkg/basl/ffi"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeThreadModule() *Env {
	env := NewEnv(nil)

	env.Define("spawn", value.NewNativeFunc("thread.spawn", func(args []value.Value) (value.Value, error) {
		if len(args) < 1 || args[0].T != value.TypeFunc {
			return value.Void, fmt.Errorf("thread.spawn: expected fn as first argument")
		}
		fnVal := args[0]
		fnArgs := args[1:]

		argsCopy := make([]value.Value, len(fnArgs))
		copy(argsCopy, fnArgs)

		var wg sync.WaitGroup
		wg.Add(1)
		var result value.Value
		var resultErr error

		cbPtr, slot, err := ffi.RegisterCallback(func(cargs []uintptr) uintptr {
			defer wg.Done()
			interp.gil.Lock()
			result, resultErr = interp.callFunc(fnVal, argsCopy)
			interp.gil.Unlock()
			return 0
		})
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindIO)}}
		}

		var tid C.HANDLE
		rc := C.basl_thread_create(&tid, (C.basl_cb_fn)(cbPtr), 0)
		if rc != 0 {
			ffi.FreeCallback(slot)
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(fmt.Sprintf("CreateThread failed: %d", rc), value.ErrKindIO)}}
		}

		obj := &value.ObjectVal{
			ClassName: "Thread",
			Fields: map[string]value.Value{
				"__tid":    {T: value.TypeString, Data: tid},
				"__slot":   value.NewI32(int32(slot)),
				"__wg":     {T: value.TypeString, Data: &wg},
				"__result": {T: value.TypeString, Data: &result},
				"__err":    {T: value.TypeString, Data: &resultErr},
				"__joined": value.False,
			},
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeObject, Data: obj}, value.Ok}}
	}))

	env.Define("sleep", value.NewNativeFunc("thread.sleep", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeI32 {
			return value.Void, fmt.Errorf("thread.sleep: expected i32 milliseconds")
		}
		ms := args[0].AsI32()
		interp.gil.Unlock()
		C.basl_sleep_ms(C.int(ms))
		interp.gil.Lock()
		return value.Ok, nil
	}))

	return env
}

func (interp *Interpreter) threadMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	switch method {
	case "join":
		return value.NewNativeFunc("Thread.join", func(args []value.Value) (value.Value, error) {
			if joined, ok := o.Fields["__joined"]; ok && joined.T == value.TypeBool && joined.AsBool() {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("thread already joined", value.ErrKindState)}}
			}
			tid, ok := o.Fields["__tid"].Data.(C.HANDLE)
			if !ok {
				return value.Void, fmt.Errorf("Thread.join: invalid thread handle")
			}
			slot := int(o.Fields["__slot"].AsI32())
			wg, ok := o.Fields["__wg"].Data.(*sync.WaitGroup)
			if !ok {
				return value.Void, fmt.Errorf("Thread.join: invalid wait group")
			}

			interp.gil.Unlock()
			rc := C.basl_thread_join(tid)
			if rc != 0 {
				interp.gil.Lock()
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(fmt.Sprintf("WaitForSingleObject failed: %d", rc), value.ErrKindIO)}}
			}
			wg.Wait()
			interp.gil.Lock()
			ffi.FreeCallback(slot)
			o.Fields["__joined"] = value.True

			result := *o.Fields["__result"].Data.(*value.Value)
			resultErr := *o.Fields["__err"].Data.(*error)
			if resultErr != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{result, value.NewErr(resultErr.Error(), value.ErrKindIO)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{result, value.Ok}}
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: Thread has no method '%s'", line, method)
	}
}

func (interp *Interpreter) makeMutexModule() *Env {
	env := NewEnv(nil)

	env.Define("new", value.NewNativeFunc("mutex.new", func(args []value.Value) (value.Value, error) {
		m := (*C.CRITICAL_SECTION)(C.malloc(C.size_t(unsafe.Sizeof(C.CRITICAL_SECTION{}))))
		rc := C.basl_mutex_init(m)
		if rc != 0 {
			C.free(unsafe.Pointer(m))
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(fmt.Sprintf("mutex init failed: %d", rc), value.ErrKindIO)}}
		}
		obj := &value.ObjectVal{
			ClassName: "Mutex",
			Fields: map[string]value.Value{
				"__mutex":     {T: value.TypeString, Data: m},
				"__destroyed": value.False,
			},
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeObject, Data: obj}, value.Ok}}
	}))

	return env
}

func (interp *Interpreter) mutexMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	m, ok := o.Fields["__mutex"].Data.(*C.CRITICAL_SECTION)
	if !ok {
		return value.Void, fmt.Errorf("line %d: Mutex is invalid", line)
	}
	switch method {
	case "lock":
		return value.NewNativeFunc("Mutex.lock", func(args []value.Value) (value.Value, error) {
			if destroyed, ok := o.Fields["__destroyed"]; ok && destroyed.T == value.TypeBool && destroyed.AsBool() {
				return value.NewErr("mutex is destroyed", value.ErrKindState), nil
			}
			rc := C.basl_mutex_lock(m)
			if rc != 0 {
				return value.NewErr(fmt.Sprintf("mutex lock failed: %d", rc), value.ErrKindIO), nil
			}
			return value.Ok, nil
		}), nil
	case "unlock":
		return value.NewNativeFunc("Mutex.unlock", func(args []value.Value) (value.Value, error) {
			if destroyed, ok := o.Fields["__destroyed"]; ok && destroyed.T == value.TypeBool && destroyed.AsBool() {
				return value.NewErr("mutex is destroyed", value.ErrKindState), nil
			}
			rc := C.basl_mutex_unlock(m)
			if rc != 0 {
				return value.NewErr(fmt.Sprintf("mutex unlock failed: %d", rc), value.ErrKindIO), nil
			}
			return value.Ok, nil
		}), nil
	case "destroy":
		return value.NewNativeFunc("Mutex.destroy", func(args []value.Value) (value.Value, error) {
			if destroyed, ok := o.Fields["__destroyed"]; ok && destroyed.T == value.TypeBool && destroyed.AsBool() {
				return value.NewErr("mutex already destroyed", value.ErrKindState), nil
			}
			rc := C.basl_mutex_destroy(m)
			if rc == 0 {
				C.free(unsafe.Pointer(m))
				o.Fields["__destroyed"] = value.True
			}
			if rc != 0 {
				return value.NewErr(fmt.Sprintf("mutex destroy failed: %d", rc), value.ErrKindIO), nil
			}
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: Mutex has no method '%s'", line, method)
	}
}
