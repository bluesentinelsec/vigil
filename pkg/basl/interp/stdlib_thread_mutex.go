package interp

/*
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>

// Thread entry wrapper: pthread wants void*(*)(void*), our callbacks are
// intptr_t(*)(intptr_t,intptr_t,intptr_t,intptr_t). This adapter bridges them.
typedef intptr_t (*basl_cb_fn)(intptr_t, intptr_t, intptr_t, intptr_t);

struct thread_ctx {
    basl_cb_fn fn;
    intptr_t   arg;
};

static void* thread_entry(void* raw) {
    struct thread_ctx* ctx = (struct thread_ctx*)raw;
    basl_cb_fn fn = ctx->fn;
    intptr_t arg = ctx->arg;
    free(ctx);
    fn(arg, 0, 0, 0);
    return NULL;
}

static int basl_thread_create(pthread_t* tid, basl_cb_fn fn, intptr_t arg) {
    struct thread_ctx* ctx = (struct thread_ctx*)malloc(sizeof(struct thread_ctx));
    ctx->fn = fn;
    ctx->arg = arg;
    return pthread_create(tid, NULL, thread_entry, ctx);
}

static int basl_thread_join(pthread_t tid) {
    return pthread_join(tid, NULL);
}

static int basl_mutex_init(pthread_mutex_t* m) {
    return pthread_mutex_init(m, NULL);
}

static int basl_mutex_lock(pthread_mutex_t* m) {
    return pthread_mutex_lock(m);
}

static int basl_mutex_unlock(pthread_mutex_t* m) {
    return pthread_mutex_unlock(m);
}

static int basl_mutex_destroy(pthread_mutex_t* m) {
    return pthread_mutex_destroy(m);
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

// ── thread module ──

func (interp *Interpreter) makeThreadModule() *Env {
	env := NewEnv(nil)

	env.Define("spawn", value.NewNativeFunc("thread.spawn", func(args []value.Value) (value.Value, error) {
		if len(args) < 1 || args[0].T != value.TypeFunc {
			return value.Void, fmt.Errorf("thread.spawn: expected fn as first argument")
		}
		fnVal := args[0]
		fnArgs := args[1:] // extra args to pass to the function

		// Create a snapshot of args for the thread
		argsCopy := make([]value.Value, len(fnArgs))
		copy(argsCopy, fnArgs)

		// Use a WaitGroup + result channel to capture completion
		var wg sync.WaitGroup
		wg.Add(1)
		var result value.Value
		var resultErr error

		// Register a C callback that will run the BASL function
		cbPtr, slot, err := ffi.RegisterCallback(func(cargs []uintptr) uintptr {
			defer wg.Done()
			interp.gil.Lock()
			result, resultErr = interp.callFunc(fnVal, argsCopy)
			interp.gil.Unlock()
			return 0
		})
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}

		// Create the OS thread via pthread_create
		var tid C.pthread_t
		rc := C.basl_thread_create(&tid, (C.basl_cb_fn)(cbPtr), 0)
		if rc != 0 {
			ffi.FreeCallback(slot)
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(fmt.Sprintf("pthread_create failed: %d", rc))}}
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
		ts := C.struct_timespec{
			tv_sec:  C.long(ms / 1000),
			tv_nsec: C.long((ms % 1000) * 1000000),
		}
		interp.gil.Unlock()
		C.nanosleep(&ts, nil)
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
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("thread already joined")}}
			}
			tid, ok := o.Fields["__tid"].Data.(C.pthread_t)
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
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(fmt.Sprintf("pthread_join failed: %d", rc))}}
			}
			// Synchronize with the Go callback before reading result fields.
			wg.Wait()
			interp.gil.Lock()
			ffi.FreeCallback(slot)
			o.Fields["__joined"] = value.True

			result := *o.Fields["__result"].Data.(*value.Value)
			resultErr := *o.Fields["__err"].Data.(*error)
			if resultErr != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{result, value.NewErr(resultErr.Error())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{result, value.Ok}}
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: Thread has no method '%s'", line, method)
	}
}

// ── mutex module ──

func (interp *Interpreter) makeMutexModule() *Env {
	env := NewEnv(nil)

	env.Define("new", value.NewNativeFunc("mutex.new", func(args []value.Value) (value.Value, error) {
		m := (*C.pthread_mutex_t)(C.malloc(C.size_t(unsafe.Sizeof(C.pthread_mutex_t{}))))
		rc := C.basl_mutex_init(m)
		if rc != 0 {
			C.free(unsafe.Pointer(m))
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(fmt.Sprintf("mutex init failed: %d", rc))}}
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
	m, ok := o.Fields["__mutex"].Data.(*C.pthread_mutex_t)
	if !ok {
		return value.Void, fmt.Errorf("line %d: Mutex is invalid", line)
	}
	switch method {
	case "lock":
		return value.NewNativeFunc("Mutex.lock", func(args []value.Value) (value.Value, error) {
			if destroyed, ok := o.Fields["__destroyed"]; ok && destroyed.T == value.TypeBool && destroyed.AsBool() {
				return value.NewErr("mutex is destroyed"), nil
			}
			rc := C.basl_mutex_lock(m)
			if rc != 0 {
				return value.NewErr(fmt.Sprintf("mutex lock failed: %d", rc)), nil
			}
			return value.Ok, nil
		}), nil
	case "unlock":
		return value.NewNativeFunc("Mutex.unlock", func(args []value.Value) (value.Value, error) {
			if destroyed, ok := o.Fields["__destroyed"]; ok && destroyed.T == value.TypeBool && destroyed.AsBool() {
				return value.NewErr("mutex is destroyed"), nil
			}
			rc := C.basl_mutex_unlock(m)
			if rc != 0 {
				return value.NewErr(fmt.Sprintf("mutex unlock failed: %d", rc)), nil
			}
			return value.Ok, nil
		}), nil
	case "destroy":
		return value.NewNativeFunc("Mutex.destroy", func(args []value.Value) (value.Value, error) {
			if destroyed, ok := o.Fields["__destroyed"]; ok && destroyed.T == value.TypeBool && destroyed.AsBool() {
				return value.NewErr("mutex already destroyed"), nil
			}
			rc := C.basl_mutex_destroy(m)
			if rc == 0 {
				C.free(unsafe.Pointer(m))
				o.Fields["__destroyed"] = value.True
			}
			if rc != 0 {
				return value.NewErr(fmt.Sprintf("mutex destroy failed: %d", rc)), nil
			}
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: Mutex has no method '%s'", line, method)
	}
}
