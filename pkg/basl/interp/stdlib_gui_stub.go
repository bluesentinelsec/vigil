//go:build !darwin || !cgo

package interp

import "fmt"

const guiUnsupportedMsg = "gui module is only available on macOS with cgo enabled"

func guiSupported() bool {
	return false
}

func guiBackendName() string {
	return "unsupported"
}

func guiAppCreate() (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiAppRun(_ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiAppQuit(_ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiWindowCreate(_ string, _ int32, _ int32) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiWindowSetTitle(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiWindowSetChild(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiWindowShow(_ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiWindowClose(_ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiBoxCreate(_ bool) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiBoxAdd(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiBoxSetSpacing(_ uintptr, _ int32) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiBoxSetPadding(_ uintptr, _ int32) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiGridCreate(_ int32, _ int32) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiGridPlace(_ uintptr, _ uintptr, _ int32, _ int32, _ int32, _ int32, _ bool, _ bool) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiLabelCreate(_ string) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiLabelSetText(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiButtonCreate(_ string) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiButtonSetText(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiButtonSetOnClick(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiEntryCreate() (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiEntryText(_ uintptr) (string, error) {
	return "", fmt.Errorf(guiUnsupportedMsg)
}

func guiEntrySetText(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiCheckboxCreate(_ string, _ bool) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiCheckboxSetText(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiCheckboxChecked(_ uintptr) (bool, error) {
	return false, fmt.Errorf(guiUnsupportedMsg)
}

func guiCheckboxSetChecked(_ uintptr, _ bool) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiCheckboxSetOnToggle(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiSelectCreate(_ []string, _ int32) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiSelectSelectedIndex(_ uintptr) (int32, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiSelectSetSelectedIndex(_ uintptr, _ int32) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiSelectSelectedText(_ uintptr) (string, error) {
	return "", fmt.Errorf(guiUnsupportedMsg)
}

func guiSelectAddItem(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiSelectSetOnChange(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiTextAreaCreate() (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiTextAreaText(_ uintptr) (string, error) {
	return "", fmt.Errorf(guiUnsupportedMsg)
}

func guiTextAreaSetText(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiTextAreaAppend(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiProgressCreate(_ float64, _ float64, _ float64, _ bool) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiProgressValue(_ uintptr) (float64, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiProgressSetValue(_ uintptr, _ float64) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiFrameCreate(_ int32) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiFrameSetChild(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiGroupCreate(_ string, _ int32) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiGroupSetChild(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiGroupSetTitle(_ uintptr, _ string) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiRadioCreate(_ []string, _ int32, _ bool) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiRadioSelectedIndex(_ uintptr) (int32, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiRadioSetSelectedIndex(_ uintptr, _ int32) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiRadioSelectedText(_ uintptr) (string, error) {
	return "", fmt.Errorf(guiUnsupportedMsg)
}

func guiRadioSetOnChange(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiScaleCreate(_ float64, _ float64, _ float64, _ bool) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiScaleValue(_ uintptr) (float64, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiScaleSetValue(_ uintptr, _ float64) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiScaleSetOnChange(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiSpinboxCreate(_ float64, _ float64, _ float64, _ float64) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiSpinboxValue(_ uintptr) (float64, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiSpinboxSetValue(_ uintptr, _ float64) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiSpinboxSetOnChange(_ uintptr, _ uintptr) error {
	return fmt.Errorf(guiUnsupportedMsg)
}

func guiSeparatorCreate(_ bool) (uintptr, error) {
	return 0, fmt.Errorf(guiUnsupportedMsg)
}

func guiWidgetSetSize(_ uintptr, _ int32, _ int32) error {
	return fmt.Errorf(guiUnsupportedMsg)
}
