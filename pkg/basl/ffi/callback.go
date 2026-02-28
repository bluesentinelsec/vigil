package ffi

/*
#include <stdint.h>

// Callback trampoline pool — 8 slots, each a C function that calls back into Go.
// The Go side registers a function ID, and the trampoline dispatches to it.

extern intptr_t goCallbackDispatch(int slot, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3);

static intptr_t cb_slot0(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3) { return goCallbackDispatch(0, a0, a1, a2, a3); }
static intptr_t cb_slot1(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3) { return goCallbackDispatch(1, a0, a1, a2, a3); }
static intptr_t cb_slot2(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3) { return goCallbackDispatch(2, a0, a1, a2, a3); }
static intptr_t cb_slot3(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3) { return goCallbackDispatch(3, a0, a1, a2, a3); }
static intptr_t cb_slot4(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3) { return goCallbackDispatch(4, a0, a1, a2, a3); }
static intptr_t cb_slot5(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3) { return goCallbackDispatch(5, a0, a1, a2, a3); }
static intptr_t cb_slot6(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3) { return goCallbackDispatch(6, a0, a1, a2, a3); }
static intptr_t cb_slot7(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3) { return goCallbackDispatch(7, a0, a1, a2, a3); }

static void* get_cb_ptr(int slot) {
    switch(slot) {
    case 0: return (void*)cb_slot0;
    case 1: return (void*)cb_slot1;
    case 2: return (void*)cb_slot2;
    case 3: return (void*)cb_slot3;
    case 4: return (void*)cb_slot4;
    case 5: return (void*)cb_slot5;
    case 6: return (void*)cb_slot6;
    case 7: return (void*)cb_slot7;
    default: return (void*)0;
    }
}
*/
import "C"
import (
	"fmt"
	"sync"
	"unsafe"
)

const MaxCallbacks = 8

// CallbackFn is the Go function signature for callbacks.
type CallbackFn func(args []uintptr) uintptr

var (
	cbMu    sync.Mutex
	cbSlots [MaxCallbacks]CallbackFn
	cbUsed  [MaxCallbacks]bool
)

// RegisterCallback allocates a callback slot and returns the C function pointer.
func RegisterCallback(fn CallbackFn) (unsafe.Pointer, int, error) {
	cbMu.Lock()
	defer cbMu.Unlock()
	for i := 0; i < MaxCallbacks; i++ {
		if !cbUsed[i] {
			cbUsed[i] = true
			cbSlots[i] = fn
			return C.get_cb_ptr(C.int(i)), i, nil
		}
	}
	return nil, -1, fmt.Errorf("ffi: all %d callback slots in use", MaxCallbacks)
}

// FreeCallback releases a callback slot.
func FreeCallback(slot int) {
	cbMu.Lock()
	defer cbMu.Unlock()
	if slot >= 0 && slot < MaxCallbacks {
		cbUsed[slot] = false
		cbSlots[slot] = nil
	}
}

//export goCallbackDispatch
func goCallbackDispatch(slot C.int, a0, a1, a2, a3 C.intptr_t) C.intptr_t {
	cbMu.Lock()
	fn := cbSlots[int(slot)]
	cbMu.Unlock()
	if fn == nil {
		return 0
	}
	args := []uintptr{uintptr(a0), uintptr(a1), uintptr(a2), uintptr(a3)}
	return C.intptr_t(fn(args))
}
