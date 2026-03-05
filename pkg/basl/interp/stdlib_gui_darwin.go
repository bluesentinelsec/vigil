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
uintptr_t basl_gui_grid_new(int32_t rowSpacing, int32_t colSpacing, char** errOut);
int basl_gui_grid_place(uintptr_t gridPtr, uintptr_t childPtr, int32_t row, int32_t col, int32_t rowSpan, int32_t colSpan, int fillX, int fillY, char** errOut);
uintptr_t basl_gui_label_new(const char* text, char** errOut);
int basl_gui_label_set_text(uintptr_t labelPtr, const char* text, char** errOut);
uintptr_t basl_gui_button_new(const char* text, char** errOut);
int basl_gui_button_set_text(uintptr_t buttonPtr, const char* text, char** errOut);
int basl_gui_button_set_on_click(uintptr_t buttonPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_entry_new(char** errOut);
char* basl_gui_entry_text(uintptr_t entryPtr, char** errOut);
int basl_gui_entry_set_text(uintptr_t entryPtr, const char* text, char** errOut);
uintptr_t basl_gui_checkbox_new(const char* text, int checked, char** errOut);
int basl_gui_checkbox_set_text(uintptr_t checkboxPtr, const char* text, char** errOut);
int basl_gui_checkbox_is_checked(uintptr_t checkboxPtr, int* checkedOut, char** errOut);
int basl_gui_checkbox_set_checked(uintptr_t checkboxPtr, int checked, char** errOut);
int basl_gui_checkbox_set_on_toggle(uintptr_t checkboxPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_select_new(const char** items, int32_t itemCount, int32_t selectedIndex, char** errOut);
int basl_gui_select_selected_index(uintptr_t selectPtr, int32_t* outIndex, char** errOut);
int basl_gui_select_set_selected_index(uintptr_t selectPtr, int32_t selectedIndex, char** errOut);
char* basl_gui_select_selected_text(uintptr_t selectPtr, char** errOut);
int basl_gui_select_add_item(uintptr_t selectPtr, const char* text, char** errOut);
int basl_gui_select_set_on_change(uintptr_t selectPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_textarea_new(char** errOut);
char* basl_gui_textarea_text(uintptr_t textareaPtr, char** errOut);
int basl_gui_textarea_set_text(uintptr_t textareaPtr, const char* text, char** errOut);
int basl_gui_textarea_append(uintptr_t textareaPtr, const char* text, char** errOut);
uintptr_t basl_gui_progress_new(double minValue, double maxValue, double value, int indeterminate, char** errOut);
int basl_gui_progress_value(uintptr_t progressPtr, double* outValue, char** errOut);
int basl_gui_progress_set_value(uintptr_t progressPtr, double value, char** errOut);
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

func guiGridCreate(rowSpacing int32, colSpacing int32) (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_grid_new(C.int32_t(rowSpacing), C.int32_t(colSpacing), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiGridPlace(gridHandle uintptr, childHandle uintptr, row int32, col int32, rowSpan int32, colSpan int32, fillX bool, fillY bool) error {
	var errOut *C.char
	fillXFlag := C.int(0)
	fillYFlag := C.int(0)
	if fillX {
		fillXFlag = 1
	}
	if fillY {
		fillYFlag = 1
	}
	ok := C.basl_gui_grid_place(
		C.uintptr_t(gridHandle),
		C.uintptr_t(childHandle),
		C.int32_t(row),
		C.int32_t(col),
		C.int32_t(rowSpan),
		C.int32_t(colSpan),
		fillXFlag,
		fillYFlag,
		&errOut,
	)
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

func guiCheckboxCreate(text string, checked bool) (uintptr, error) {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	checkedFlag := C.int(0)
	if checked {
		checkedFlag = 1
	}
	handle := C.basl_gui_checkbox_new(cText, checkedFlag, &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiCheckboxSetText(checkboxHandle uintptr, text string) error {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	ok := C.basl_gui_checkbox_set_text(C.uintptr_t(checkboxHandle), cText, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiCheckboxChecked(checkboxHandle uintptr) (bool, error) {
	var errOut *C.char
	var checkedOut C.int
	ok := C.basl_gui_checkbox_is_checked(C.uintptr_t(checkboxHandle), &checkedOut, &errOut)
	if ok == 0 {
		return false, guiErr(errOut)
	}
	return checkedOut != 0, nil
}

func guiCheckboxSetChecked(checkboxHandle uintptr, checked bool) error {
	var errOut *C.char
	checkedFlag := C.int(0)
	if checked {
		checkedFlag = 1
	}
	ok := C.basl_gui_checkbox_set_checked(C.uintptr_t(checkboxHandle), checkedFlag, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiCheckboxSetOnToggle(checkboxHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_checkbox_set_on_toggle(C.uintptr_t(checkboxHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiSelectCreate(options []string, selectedIndex int32) (uintptr, error) {
	var errOut *C.char
	var cItemsRaw unsafe.Pointer
	var cItems **C.char
	if len(options) > 0 {
		cItemsRaw = C.malloc(C.size_t(len(options)) * C.size_t(unsafe.Sizeof(uintptr(0))))
		defer C.free(cItemsRaw)
		itemsSlice := unsafe.Slice((**C.char)(cItemsRaw), len(options))
		cStrs := make([]*C.char, len(options))
		for i, opt := range options {
			cStrs[i] = C.CString(opt)
			defer C.free(unsafe.Pointer(cStrs[i]))
			itemsSlice[i] = cStrs[i]
		}
		cItems = (**C.char)(cItemsRaw)
	}
	handle := C.basl_gui_select_new(cItems, C.int32_t(len(options)), C.int32_t(selectedIndex), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiSelectSelectedIndex(selectHandle uintptr) (int32, error) {
	var errOut *C.char
	var outIndex C.int32_t
	ok := C.basl_gui_select_selected_index(C.uintptr_t(selectHandle), &outIndex, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return int32(outIndex), nil
}

func guiSelectSetSelectedIndex(selectHandle uintptr, selectedIndex int32) error {
	var errOut *C.char
	ok := C.basl_gui_select_set_selected_index(C.uintptr_t(selectHandle), C.int32_t(selectedIndex), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiSelectSelectedText(selectHandle uintptr) (string, error) {
	var errOut *C.char
	text := C.basl_gui_select_selected_text(C.uintptr_t(selectHandle), &errOut)
	if text == nil {
		if err := guiErr(errOut); err != nil {
			return "", err
		}
		return "", nil
	}
	out := C.GoString(text)
	C.basl_gui_free_string(text)
	return out, nil
}

func guiSelectAddItem(selectHandle uintptr, item string) error {
	cItem := C.CString(item)
	defer C.free(unsafe.Pointer(cItem))
	var errOut *C.char
	ok := C.basl_gui_select_add_item(C.uintptr_t(selectHandle), cItem, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiSelectSetOnChange(selectHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_select_set_on_change(C.uintptr_t(selectHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiTextAreaCreate() (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_textarea_new(&errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiTextAreaText(textAreaHandle uintptr) (string, error) {
	var errOut *C.char
	text := C.basl_gui_textarea_text(C.uintptr_t(textAreaHandle), &errOut)
	if text == nil {
		if err := guiErr(errOut); err != nil {
			return "", err
		}
		return "", fmt.Errorf("failed to read text area text")
	}
	out := C.GoString(text)
	C.basl_gui_free_string(text)
	return out, nil
}

func guiTextAreaSetText(textAreaHandle uintptr, text string) error {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	ok := C.basl_gui_textarea_set_text(C.uintptr_t(textAreaHandle), cText, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiTextAreaAppend(textAreaHandle uintptr, text string) error {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	ok := C.basl_gui_textarea_append(C.uintptr_t(textAreaHandle), cText, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiProgressCreate(min float64, max float64, current float64, indeterminate bool) (uintptr, error) {
	var errOut *C.char
	indeterminateFlag := C.int(0)
	if indeterminate {
		indeterminateFlag = 1
	}
	handle := C.basl_gui_progress_new(C.double(min), C.double(max), C.double(current), indeterminateFlag, &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiProgressValue(progressHandle uintptr) (float64, error) {
	var errOut *C.char
	var outValue C.double
	ok := C.basl_gui_progress_value(C.uintptr_t(progressHandle), &outValue, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return float64(outValue), nil
}

func guiProgressSetValue(progressHandle uintptr, current float64) error {
	var errOut *C.char
	ok := C.basl_gui_progress_set_value(C.uintptr_t(progressHandle), C.double(current), &errOut)
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
