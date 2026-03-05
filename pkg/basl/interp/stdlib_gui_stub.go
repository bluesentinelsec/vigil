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

func guiWidgetSetSize(_ uintptr, _ int32, _ int32) error {
	return fmt.Errorf(guiUnsupportedMsg)
}
