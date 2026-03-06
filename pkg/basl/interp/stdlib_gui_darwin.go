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
uintptr_t basl_gui_frame_new(int32_t padding, char** errOut);
int basl_gui_frame_set_child(uintptr_t framePtr, uintptr_t childPtr, char** errOut);
uintptr_t basl_gui_group_new(const char* title, int32_t padding, char** errOut);
int basl_gui_group_set_child(uintptr_t groupPtr, uintptr_t childPtr, char** errOut);
int basl_gui_group_set_title(uintptr_t groupPtr, const char* title, char** errOut);
uintptr_t basl_gui_radio_new(const char** items, int32_t itemCount, int32_t selectedIndex, int vertical, char** errOut);
int basl_gui_radio_selected_index(uintptr_t radioPtr, int32_t* outIndex, char** errOut);
int basl_gui_radio_set_selected_index(uintptr_t radioPtr, int32_t selectedIndex, char** errOut);
char* basl_gui_radio_selected_text(uintptr_t radioPtr, char** errOut);
int basl_gui_radio_set_on_change(uintptr_t radioPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_scale_new(double minValue, double maxValue, double value, int vertical, char** errOut);
int basl_gui_scale_value(uintptr_t scalePtr, double* outValue, char** errOut);
int basl_gui_scale_set_value(uintptr_t scalePtr, double value, char** errOut);
int basl_gui_scale_set_on_change(uintptr_t scalePtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_spinbox_new(double minValue, double maxValue, double step, double value, char** errOut);
int basl_gui_spinbox_value(uintptr_t spinboxPtr, double* outValue, char** errOut);
int basl_gui_spinbox_set_value(uintptr_t spinboxPtr, double value, char** errOut);
int basl_gui_spinbox_set_on_change(uintptr_t spinboxPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_separator_new(int vertical, char** errOut);
uintptr_t basl_gui_tabs_new(int32_t selectedIndex, char** errOut);
int basl_gui_tabs_add_tab(uintptr_t tabsPtr, const char* title, uintptr_t childPtr, char** errOut);
int basl_gui_tabs_selected_index(uintptr_t tabsPtr, int32_t* outIndex, char** errOut);
int basl_gui_tabs_set_selected_index(uintptr_t tabsPtr, int32_t selectedIndex, char** errOut);
int basl_gui_tabs_set_on_change(uintptr_t tabsPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_paned_new(int vertical, double ratio, char** errOut);
int basl_gui_paned_set_first(uintptr_t panedPtr, uintptr_t childPtr, char** errOut);
int basl_gui_paned_set_second(uintptr_t panedPtr, uintptr_t childPtr, char** errOut);
int basl_gui_paned_ratio(uintptr_t panedPtr, double* outRatio, char** errOut);
int basl_gui_paned_set_ratio(uintptr_t panedPtr, double ratio, char** errOut);
int basl_gui_paned_set_on_change(uintptr_t panedPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_list_new(const char** items, int32_t itemCount, int32_t selectedIndex, char** errOut);
int basl_gui_list_selected_index(uintptr_t listPtr, int32_t* outIndex, char** errOut);
int basl_gui_list_set_selected_index(uintptr_t listPtr, int32_t selectedIndex, char** errOut);
char* basl_gui_list_selected_text(uintptr_t listPtr, char** errOut);
int basl_gui_list_add_item(uintptr_t listPtr, const char* text, char** errOut);
int basl_gui_list_clear(uintptr_t listPtr, char** errOut);
int basl_gui_list_set_on_change(uintptr_t listPtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_tree_new(char** errOut);
int basl_gui_tree_add_root(uintptr_t treePtr, const char* title, int32_t* outNodeID, char** errOut);
int basl_gui_tree_add_child(uintptr_t treePtr, int32_t parentID, const char* title, int32_t* outNodeID, char** errOut);
int basl_gui_tree_set_text(uintptr_t treePtr, int32_t nodeID, const char* title, char** errOut);
int basl_gui_tree_selected_id(uintptr_t treePtr, int32_t* outNodeID, char** errOut);
int basl_gui_tree_set_selected_id(uintptr_t treePtr, int32_t nodeID, char** errOut);
char* basl_gui_tree_selected_text(uintptr_t treePtr, char** errOut);
int basl_gui_tree_clear(uintptr_t treePtr, char** errOut);
int basl_gui_tree_set_on_change(uintptr_t treePtr, uintptr_t callbackId, char** errOut);
uintptr_t basl_gui_menu_bar_new(char** errOut);
int basl_gui_app_set_menu_bar(uintptr_t menuBarPtr, char** errOut);
uintptr_t basl_gui_menu_new(const char* title, char** errOut);
int basl_gui_menu_bar_add_menu(uintptr_t menuBarPtr, uintptr_t menuPtr, char** errOut);
int basl_gui_menu_add_item(uintptr_t menuPtr, const char* title, uintptr_t callbackId, char** errOut);
int basl_gui_menu_add_separator(uintptr_t menuPtr, char** errOut);
int basl_gui_menu_add_submenu(uintptr_t menuPtr, uintptr_t subMenuPtr, char** errOut);
uintptr_t basl_gui_canvas_new(int32_t width, int32_t height, char** errOut);
int basl_gui_canvas_clear(uintptr_t canvasPtr, char** errOut);
int basl_gui_canvas_set_color(uintptr_t canvasPtr, double r, double g, double b, double a, char** errOut);
int basl_gui_canvas_line(uintptr_t canvasPtr, double x1, double y1, double x2, double y2, double width, char** errOut);
int basl_gui_canvas_rect(uintptr_t canvasPtr, double x, double y, double w, double h, int fill, double lineWidth, double cornerRadius, char** errOut);
int basl_gui_canvas_circle(uintptr_t canvasPtr, double x, double y, double radius, int fill, double lineWidth, char** errOut);
int basl_gui_canvas_text(uintptr_t canvasPtr, double x, double y, const char* text, double size, char** errOut);
char* basl_gui_dialog_open_file(const char* title, const char* directory, const char** extensions, int32_t extCount, char** errOut);
char* basl_gui_dialog_save_file(const char* title, const char* directory, const char* fileName, const char** extensions, int32_t extCount, char** errOut);
char* basl_gui_dialog_open_directory(const char* title, const char* directory, char** errOut);
int basl_gui_dialog_info(const char* title, const char* message, char** errOut);
int basl_gui_dialog_warn(const char* title, const char* message, char** errOut);
int basl_gui_dialog_error(const char* title, const char* message, char** errOut);
int basl_gui_dialog_confirm(const char* title, const char* message, int* outConfirmed, char** errOut);
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

func guiFrameCreate(padding int32) (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_frame_new(C.int32_t(padding), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiFrameSetChild(frameHandle uintptr, childHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_frame_set_child(C.uintptr_t(frameHandle), C.uintptr_t(childHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiGroupCreate(title string, padding int32) (uintptr, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	handle := C.basl_gui_group_new(cTitle, C.int32_t(padding), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiGroupSetChild(groupHandle uintptr, childHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_group_set_child(C.uintptr_t(groupHandle), C.uintptr_t(childHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiGroupSetTitle(groupHandle uintptr, title string) error {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	ok := C.basl_gui_group_set_title(C.uintptr_t(groupHandle), cTitle, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiRadioCreate(options []string, selectedIndex int32, vertical bool) (uintptr, error) {
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
	verticalFlag := C.int(0)
	if vertical {
		verticalFlag = 1
	}
	handle := C.basl_gui_radio_new(cItems, C.int32_t(len(options)), C.int32_t(selectedIndex), verticalFlag, &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiRadioSelectedIndex(radioHandle uintptr) (int32, error) {
	var errOut *C.char
	var outIndex C.int32_t
	ok := C.basl_gui_radio_selected_index(C.uintptr_t(radioHandle), &outIndex, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return int32(outIndex), nil
}

func guiRadioSetSelectedIndex(radioHandle uintptr, selectedIndex int32) error {
	var errOut *C.char
	ok := C.basl_gui_radio_set_selected_index(C.uintptr_t(radioHandle), C.int32_t(selectedIndex), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiRadioSelectedText(radioHandle uintptr) (string, error) {
	var errOut *C.char
	text := C.basl_gui_radio_selected_text(C.uintptr_t(radioHandle), &errOut)
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

func guiRadioSetOnChange(radioHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_radio_set_on_change(C.uintptr_t(radioHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiScaleCreate(min float64, max float64, current float64, vertical bool) (uintptr, error) {
	var errOut *C.char
	verticalFlag := C.int(0)
	if vertical {
		verticalFlag = 1
	}
	handle := C.basl_gui_scale_new(C.double(min), C.double(max), C.double(current), verticalFlag, &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiScaleValue(scaleHandle uintptr) (float64, error) {
	var errOut *C.char
	var outValue C.double
	ok := C.basl_gui_scale_value(C.uintptr_t(scaleHandle), &outValue, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return float64(outValue), nil
}

func guiScaleSetValue(scaleHandle uintptr, current float64) error {
	var errOut *C.char
	ok := C.basl_gui_scale_set_value(C.uintptr_t(scaleHandle), C.double(current), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiScaleSetOnChange(scaleHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_scale_set_on_change(C.uintptr_t(scaleHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiSpinboxCreate(min float64, max float64, step float64, current float64) (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_spinbox_new(C.double(min), C.double(max), C.double(step), C.double(current), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiSpinboxValue(spinboxHandle uintptr) (float64, error) {
	var errOut *C.char
	var outValue C.double
	ok := C.basl_gui_spinbox_value(C.uintptr_t(spinboxHandle), &outValue, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return float64(outValue), nil
}

func guiSpinboxSetValue(spinboxHandle uintptr, current float64) error {
	var errOut *C.char
	ok := C.basl_gui_spinbox_set_value(C.uintptr_t(spinboxHandle), C.double(current), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiSpinboxSetOnChange(spinboxHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_spinbox_set_on_change(C.uintptr_t(spinboxHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiSeparatorCreate(vertical bool) (uintptr, error) {
	var errOut *C.char
	verticalFlag := C.int(0)
	if vertical {
		verticalFlag = 1
	}
	handle := C.basl_gui_separator_new(verticalFlag, &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiTabsCreate(selectedIndex int32) (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_tabs_new(C.int32_t(selectedIndex), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiTabsAddTab(tabsHandle uintptr, title string, childHandle uintptr) error {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	ok := C.basl_gui_tabs_add_tab(C.uintptr_t(tabsHandle), cTitle, C.uintptr_t(childHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiTabsSelectedIndex(tabsHandle uintptr) (int32, error) {
	var errOut *C.char
	var outIndex C.int32_t
	ok := C.basl_gui_tabs_selected_index(C.uintptr_t(tabsHandle), &outIndex, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return int32(outIndex), nil
}

func guiTabsSetSelectedIndex(tabsHandle uintptr, selectedIndex int32) error {
	var errOut *C.char
	ok := C.basl_gui_tabs_set_selected_index(C.uintptr_t(tabsHandle), C.int32_t(selectedIndex), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiTabsSetOnChange(tabsHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_tabs_set_on_change(C.uintptr_t(tabsHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiPanedCreate(vertical bool, ratio float64) (uintptr, error) {
	var errOut *C.char
	verticalFlag := C.int(0)
	if vertical {
		verticalFlag = 1
	}
	handle := C.basl_gui_paned_new(verticalFlag, C.double(ratio), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiPanedSetFirst(panedHandle uintptr, childHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_paned_set_first(C.uintptr_t(panedHandle), C.uintptr_t(childHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiPanedSetSecond(panedHandle uintptr, childHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_paned_set_second(C.uintptr_t(panedHandle), C.uintptr_t(childHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiPanedRatio(panedHandle uintptr) (float64, error) {
	var errOut *C.char
	var outRatio C.double
	ok := C.basl_gui_paned_ratio(C.uintptr_t(panedHandle), &outRatio, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return float64(outRatio), nil
}

func guiPanedSetRatio(panedHandle uintptr, ratio float64) error {
	var errOut *C.char
	ok := C.basl_gui_paned_set_ratio(C.uintptr_t(panedHandle), C.double(ratio), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiPanedSetOnChange(panedHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_paned_set_on_change(C.uintptr_t(panedHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiListCreate(items []string, selectedIndex int32) (uintptr, error) {
	var errOut *C.char
	var cItemsRaw unsafe.Pointer
	var cItems **C.char
	if len(items) > 0 {
		cItemsRaw = C.malloc(C.size_t(len(items)) * C.size_t(unsafe.Sizeof(uintptr(0))))
		defer C.free(cItemsRaw)
		itemsSlice := unsafe.Slice((**C.char)(cItemsRaw), len(items))
		cStrs := make([]*C.char, len(items))
		for i, item := range items {
			cStrs[i] = C.CString(item)
			defer C.free(unsafe.Pointer(cStrs[i]))
			itemsSlice[i] = cStrs[i]
		}
		cItems = (**C.char)(cItemsRaw)
	}
	handle := C.basl_gui_list_new(cItems, C.int32_t(len(items)), C.int32_t(selectedIndex), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiListSelectedIndex(listHandle uintptr) (int32, error) {
	var errOut *C.char
	var outIndex C.int32_t
	ok := C.basl_gui_list_selected_index(C.uintptr_t(listHandle), &outIndex, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return int32(outIndex), nil
}

func guiListSetSelectedIndex(listHandle uintptr, selectedIndex int32) error {
	var errOut *C.char
	ok := C.basl_gui_list_set_selected_index(C.uintptr_t(listHandle), C.int32_t(selectedIndex), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiListSelectedText(listHandle uintptr) (string, error) {
	var errOut *C.char
	text := C.basl_gui_list_selected_text(C.uintptr_t(listHandle), &errOut)
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

func guiListAddItem(listHandle uintptr, item string) error {
	cItem := C.CString(item)
	defer C.free(unsafe.Pointer(cItem))
	var errOut *C.char
	ok := C.basl_gui_list_add_item(C.uintptr_t(listHandle), cItem, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiListClear(listHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_list_clear(C.uintptr_t(listHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiListSetOnChange(listHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_list_set_on_change(C.uintptr_t(listHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiTreeCreate() (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_tree_new(&errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiTreeAddRoot(treeHandle uintptr, title string) (int32, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	var outNodeID C.int32_t
	ok := C.basl_gui_tree_add_root(C.uintptr_t(treeHandle), cTitle, &outNodeID, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return int32(outNodeID), nil
}

func guiTreeAddChild(treeHandle uintptr, parentID int32, title string) (int32, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	var outNodeID C.int32_t
	ok := C.basl_gui_tree_add_child(C.uintptr_t(treeHandle), C.int32_t(parentID), cTitle, &outNodeID, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return int32(outNodeID), nil
}

func guiTreeSetText(treeHandle uintptr, nodeID int32, title string) error {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	ok := C.basl_gui_tree_set_text(C.uintptr_t(treeHandle), C.int32_t(nodeID), cTitle, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiTreeSelectedID(treeHandle uintptr) (int32, error) {
	var errOut *C.char
	var outNodeID C.int32_t
	ok := C.basl_gui_tree_selected_id(C.uintptr_t(treeHandle), &outNodeID, &errOut)
	if ok == 0 {
		return 0, guiErr(errOut)
	}
	return int32(outNodeID), nil
}

func guiTreeSetSelectedID(treeHandle uintptr, nodeID int32) error {
	var errOut *C.char
	ok := C.basl_gui_tree_set_selected_id(C.uintptr_t(treeHandle), C.int32_t(nodeID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiTreeSelectedText(treeHandle uintptr) (string, error) {
	var errOut *C.char
	text := C.basl_gui_tree_selected_text(C.uintptr_t(treeHandle), &errOut)
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

func guiTreeClear(treeHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_tree_clear(C.uintptr_t(treeHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiTreeSetOnChange(treeHandle uintptr, callbackID uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_tree_set_on_change(C.uintptr_t(treeHandle), C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiMenuBarCreate() (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_menu_bar_new(&errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiAppSetMenuBar(_ uintptr, menuBarHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_app_set_menu_bar(C.uintptr_t(menuBarHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiMenuCreate(title string) (uintptr, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	handle := C.basl_gui_menu_new(cTitle, &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiMenuBarAddMenu(menuBarHandle uintptr, menuHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_menu_bar_add_menu(C.uintptr_t(menuBarHandle), C.uintptr_t(menuHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiMenuAddItem(menuHandle uintptr, title string, callbackID uintptr) error {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	var errOut *C.char
	ok := C.basl_gui_menu_add_item(C.uintptr_t(menuHandle), cTitle, C.uintptr_t(callbackID), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiMenuAddSeparator(menuHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_menu_add_separator(C.uintptr_t(menuHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiMenuAddSubMenu(menuHandle uintptr, subMenuHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_menu_add_submenu(C.uintptr_t(menuHandle), C.uintptr_t(subMenuHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiCanvasCreate(width int32, height int32) (uintptr, error) {
	var errOut *C.char
	handle := C.basl_gui_canvas_new(C.int32_t(width), C.int32_t(height), &errOut)
	if handle == 0 {
		return 0, guiErr(errOut)
	}
	return uintptr(handle), nil
}

func guiCanvasClear(canvasHandle uintptr) error {
	var errOut *C.char
	ok := C.basl_gui_canvas_clear(C.uintptr_t(canvasHandle), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiCanvasSetColor(canvasHandle uintptr, r float64, g float64, b float64, a float64) error {
	var errOut *C.char
	ok := C.basl_gui_canvas_set_color(C.uintptr_t(canvasHandle), C.double(r), C.double(g), C.double(b), C.double(a), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiCanvasLine(canvasHandle uintptr, x1 float64, y1 float64, x2 float64, y2 float64, width float64) error {
	var errOut *C.char
	ok := C.basl_gui_canvas_line(C.uintptr_t(canvasHandle), C.double(x1), C.double(y1), C.double(x2), C.double(y2), C.double(width), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiCanvasRect(canvasHandle uintptr, x float64, y float64, w float64, h float64, fill bool, lineWidth float64, cornerRadius float64) error {
	var errOut *C.char
	fillFlag := C.int(0)
	if fill {
		fillFlag = 1
	}
	ok := C.basl_gui_canvas_rect(C.uintptr_t(canvasHandle), C.double(x), C.double(y), C.double(w), C.double(h), fillFlag, C.double(lineWidth), C.double(cornerRadius), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiCanvasCircle(canvasHandle uintptr, x float64, y float64, radius float64, fill bool, lineWidth float64) error {
	var errOut *C.char
	fillFlag := C.int(0)
	if fill {
		fillFlag = 1
	}
	ok := C.basl_gui_canvas_circle(C.uintptr_t(canvasHandle), C.double(x), C.double(y), C.double(radius), fillFlag, C.double(lineWidth), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiCanvasText(canvasHandle uintptr, x float64, y float64, text string, size float64) error {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var errOut *C.char
	ok := C.basl_gui_canvas_text(C.uintptr_t(canvasHandle), C.double(x), C.double(y), cText, C.double(size), &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiMakeCStringArray(values []string) (unsafe.Pointer, **C.char, func()) {
	if len(values) == 0 {
		return nil, nil, func() {}
	}
	raw := C.malloc(C.size_t(len(values)) * C.size_t(unsafe.Sizeof(uintptr(0))))
	items := unsafe.Slice((**C.char)(raw), len(values))
	cStrs := make([]*C.char, len(values))
	for i, v := range values {
		cStrs[i] = C.CString(v)
		items[i] = cStrs[i]
	}
	cleanup := func() {
		for _, cStr := range cStrs {
			C.free(unsafe.Pointer(cStr))
		}
		C.free(raw)
	}
	return raw, (**C.char)(raw), cleanup
}

func guiDialogOpenFile(title string, directory string, extensions []string) (string, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	cDir := C.CString(directory)
	defer C.free(unsafe.Pointer(cDir))
	raw, cExts, cleanup := guiMakeCStringArray(extensions)
	_ = raw
	defer cleanup()
	var errOut *C.char
	result := C.basl_gui_dialog_open_file(cTitle, cDir, cExts, C.int32_t(len(extensions)), &errOut)
	if result == nil {
		if err := guiErr(errOut); err != nil {
			return "", err
		}
		return "", nil
	}
	out := C.GoString(result)
	C.basl_gui_free_string(result)
	return out, nil
}

func guiDialogSaveFile(title string, directory string, fileName string, extensions []string) (string, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	cDir := C.CString(directory)
	defer C.free(unsafe.Pointer(cDir))
	cName := C.CString(fileName)
	defer C.free(unsafe.Pointer(cName))
	raw, cExts, cleanup := guiMakeCStringArray(extensions)
	_ = raw
	defer cleanup()
	var errOut *C.char
	result := C.basl_gui_dialog_save_file(cTitle, cDir, cName, cExts, C.int32_t(len(extensions)), &errOut)
	if result == nil {
		if err := guiErr(errOut); err != nil {
			return "", err
		}
		return "", nil
	}
	out := C.GoString(result)
	C.basl_gui_free_string(result)
	return out, nil
}

func guiDialogOpenDirectory(title string, directory string) (string, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	cDir := C.CString(directory)
	defer C.free(unsafe.Pointer(cDir))
	var errOut *C.char
	result := C.basl_gui_dialog_open_directory(cTitle, cDir, &errOut)
	if result == nil {
		if err := guiErr(errOut); err != nil {
			return "", err
		}
		return "", nil
	}
	out := C.GoString(result)
	C.basl_gui_free_string(result)
	return out, nil
}

func guiDialogInfo(title string, message string) error {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	cMessage := C.CString(message)
	defer C.free(unsafe.Pointer(cMessage))
	var errOut *C.char
	ok := C.basl_gui_dialog_info(cTitle, cMessage, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiDialogWarn(title string, message string) error {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	cMessage := C.CString(message)
	defer C.free(unsafe.Pointer(cMessage))
	var errOut *C.char
	ok := C.basl_gui_dialog_warn(cTitle, cMessage, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiDialogError(title string, message string) error {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	cMessage := C.CString(message)
	defer C.free(unsafe.Pointer(cMessage))
	var errOut *C.char
	ok := C.basl_gui_dialog_error(cTitle, cMessage, &errOut)
	if ok == 0 {
		return guiErr(errOut)
	}
	return nil
}

func guiDialogConfirm(title string, message string) (bool, error) {
	cTitle := C.CString(title)
	defer C.free(unsafe.Pointer(cTitle))
	cMessage := C.CString(message)
	defer C.free(unsafe.Pointer(cMessage))
	var errOut *C.char
	var confirmed C.int
	ok := C.basl_gui_dialog_confirm(cTitle, cMessage, &confirmed, &errOut)
	if ok == 0 {
		return false, guiErr(errOut)
	}
	return confirmed != 0, nil
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
