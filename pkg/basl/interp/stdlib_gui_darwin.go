//go:build darwin && cgo

package interp

/*
#cgo CFLAGS: -x objective-c -fobjc-arc
#cgo LDFLAGS: -framework Cocoa
#include <stdint.h>
#include <stdlib.h>

uintptr_t basl_gui_app_new(char** errOut);
int basl_gui_app_run(char** errOut);
int basl_gui_app_quit(char** errOut);
uintptr_t basl_gui_window_new(const char* title, int32_t width, int32_t height, char** errOut);
int basl_gui_window_set_title(uintptr_t windowPtr, const char* title, char** errOut);
int basl_gui_window_set_child(uintptr_t windowPtr, uintptr_t viewPtr, char** errOut);
int basl_gui_window_show(uintptr_t windowPtr, char** errOut);
int basl_gui_window_close(uintptr_t windowPtr, char** errOut);
uintptr_t basl_gui_box_new(int vertical, char** errOut);
int basl_gui_box_add(uintptr_t boxPtr, uintptr_t childPtr, char** errOut);
int basl_gui_box_set_spacing(uintptr_t boxPtr, int32_t spacing, char** errOut);
int basl_gui_box_set_padding(uintptr_t boxPtr, int32_t padding, char** errOut);
uintptr_t basl_gui_label_new(const char* text, char** errOut);
int basl_gui_label_set_text(uintptr_t labelPtr, const char* text, char** errOut);
uintptr_t basl_gui_button_new(const char* text, char** errOut);
int basl_gui_button_set_text(uintptr_t buttonPtr, const char* text, char** errOut);
int basl_gui_button_set_on_click(uintptr_t buttonPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_entry_new(char** errOut);
char* basl_gui_entry_text(uintptr_t entryPtr, char** errOut);
int basl_gui_entry_set_text(uintptr_t entryPtr, const char* text, char** errOut);
int basl_gui_widget_set_size(uintptr_t viewPtr, int32_t width, int32_t height, char** errOut);
void basl_gui_free_string(char* s);
*/
import "C"

import (
	"fmt"
	"runtime"
	"sync"
	"unsafe"
)

var guiThreadOnce sync.Once

func guiSupported() bool {
	return true
}

func guiBackendName() string {
	return "cocoa"
}

func guiErr(errOut *C.char) error {
	if errOut == nil {
		return nil
	}
	msg := C.GoString(errOut)
	C.basl_gui_free_string(errOut)
	return fmt.Errorf("%s", msg)
}

func guiAppCreate() (uintptr, error) {
	guiThreadOnce.Do(runtime.LockOSThread)
	var errOut *C.char
	handle := C.basl_gui_app_new(&errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiAppRun(_ uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_app_run(&errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiAppQuit(_ uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_app_quit(&errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiWindowCreate(title string, width int32, height int32) (uintptr, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	handle := C.basl_gui_window_new(cTitle, C.int32_t(width), C.int32_t(height), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiWindowSetTitle(windowHandle uintptr, title string) error {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	ok := C.basl_gui_window_set_title(C.uintptr_t(windowHandle), cTitle, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiWindowSetChild(windowHandle uintptr, childHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_window_set_child(C.uintptr_t(windowHandle), C.uintptr_t(childHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiWindowShow(windowHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_window_show(C.uintptr_t(windowHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiWindowClose(windowHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_window_close(C.uintptr_t(windowHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiBoxCreate(vertical bool) (uintptr, error) {
	var errOut *C.char
	v := 0
	if vertical {
		v = 1
	}
	handle := C.basl_gui_box_new(C.int(v), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiBoxAdd(boxHandle uintptr, childHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_box_add(C.uintptr_t(boxHandle), C.uintptr_t(childHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiBoxSetSpacing(boxHandle uintptr, spacing int32) error {
	var errOut *C.char
	ok := C.basl_gui_box_set_spacing(C.uintptr_t(boxHandle), C.int32_t(spacing), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiBoxSetPadding(boxHandle uintptr, padding int32) error {
	var errOut *C.char
	ok := C.basl_gui_box_set_padding(C.uintptr_t(boxHandle), C.int32_t(padding), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiLabelCreate(text string) (uintptr, error) {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	handle := C.basl_gui_label_new(cText, &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiLabelSetText(labelHandle uintptr, text string) error {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	ok := C.basl_gui_label_set_text(C.uintptr_t(labelHandle), cText, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiButtonCreate(text string) (uintptr, error) {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	handle := C.basl_gui_button_new(cText, &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiButtonSetText(buttonHandle uintptr, text string) error {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	ok := C.basl_gui_button_set_text(C.uintptr_t(buttonHandle), cText, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiButtonSetOnClick(buttonHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_button_set_on_click(C.uintptr_t(buttonHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiEntryCreate() (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_entry_new(&errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiEntryText(entryHandle uintptr) (string, error) {
	var errOut *C.char
	text := C.basl_gui_entry_text(C.uintptr_t(entryHandle), &errOut)
	if text == nil {
		if err := guiErr(errOut); err != nil {
			return "", err
		}
		return "", fmt.Errorf("failed to read entry text")
	}
	out := C.GoString(text)
	C.basl_gui_free_string(text)
	return out, nil
}

func guiEntrySetText(entryHandle uintptr, text string) error {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	ok := C.basl_gui_entry_set_text(C.uintptr_t(entryHandle), cText, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiWidgetSetSize(widgetHandle uintptr, width int32, height int32) error {
	var errOut *C.char
	ok := C.basl_gui_widget_set_size(C.uintptr_t(widgetHandle), C.int32_t(width), C.int32_t(height), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

//export baslGuiInvokeCallback
func baslGuiInvokeCallback(callbackID C.uintptr_t) {
	invokeGuiCallback(uintptr(callbackID))
}
