package interp

import (
	"fmt"
	"sync"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

const (
	guiClassApp    = "gui.App"
	guiClassWindow = "gui.Window"
	guiClassBox    = "gui.Box"
	guiClassLabel  = "gui.Label"
	guiClassButton = "gui.Button"
	guiClassEntry  = "gui.Entry"

	guiClassAppOpts    = "gui.AppOpts"
	guiClassWindowOpts = "gui.WindowOpts"
	guiClassBoxOpts    = "gui.BoxOpts"
	guiClassLabelOpts  = "gui.LabelOpts"
	guiClassButtonOpts = "gui.ButtonOpts"
	guiClassEntryOpts  = "gui.EntryOpts"
)

type guiCallback struct {
	interp *Interpreter
	fn     value.Value
}

var (
	guiCallbacksMu sync.Mutex
	guiCallbacks           = map[uintptr]guiCallback{}
	guiNextCbID    uintptr = 1
)

func registerGuiCallback(interp *Interpreter, fn value.Value) uintptr {
	guiCallbacksMu.Lock()
	defer guiCallbacksMu.Unlock()
	id := guiNextCbID
	guiNextCbID++
	guiCallbacks[id] = guiCallback{interp: interp, fn: fn}
	return id
}

func invokeGuiCallback(id uintptr) {
	guiCallbacksMu.Lock()
	cb, ok := guiCallbacks[id]
	guiCallbacksMu.Unlock()
	if !ok {
		return
	}

	result, err := cb.interp.callFunc(cb.fn, nil)
	if err != nil {
		cb.interp.ErrFn(fmt.Sprintf("gui callback error: %v\n", err))
		return
	}
	if result.T == value.TypeErr && !result.IsOk() {
		cb.interp.ErrFn(fmt.Sprintf("gui callback returned error: %s\n", result.AsErr().Message))
	}
}

func newGuiObject(className string, handle uintptr, methods map[string]value.Value) value.Value {
	fields := map[string]value.Value{
		"__handle": value.NewPtr(handle),
	}
	for name, method := range methods {
		fields[name] = method
	}
	return value.Value{
		T: value.TypeObject,
		Data: &value.ObjectVal{
			ClassName: className,
			Fields:    fields,
		},
	}
}

func newGuiOptsObject(className string, fields map[string]value.Value) value.Value {
	if fields == nil {
		fields = make(map[string]value.Value)
	}
	return value.Value{
		T: value.TypeObject,
		Data: &value.ObjectVal{
			ClassName: className,
			Fields:    fields,
		},
	}
}

func guiWidgetHandle(v value.Value) (uintptr, error) {
	if v.T != value.TypeObject {
		return 0, fmt.Errorf("expected gui widget object")
	}
	obj := v.AsObject()
	switch obj.ClassName {
	case guiClassBox, guiClassLabel, guiClassButton, guiClassEntry:
	default:
		return 0, fmt.Errorf("expected gui widget, got %s", obj.ClassName)
	}
	handle, ok := obj.Fields["__handle"]
	if !ok || handle.T != value.TypePtr {
		return 0, fmt.Errorf("invalid gui widget handle")
	}
	return handle.AsPtr(), nil
}

func guiStateErr(err error) value.Value {
	return value.NewErr(err.Error(), value.ErrKindState)
}

func guiExpectOpts(args []value.Value, className string, fnName string) (*value.ObjectVal, error) {
	if len(args) != 1 || args[0].T != value.TypeObject {
		return nil, fmt.Errorf("%s: expected %s", fnName, className)
	}
	obj := args[0].AsObject()
	if obj.ClassName != className {
		return nil, fmt.Errorf("%s: expected %s, got %s", fnName, className, obj.ClassName)
	}
	return obj, nil
}

func guiReadStringOpt(obj *value.ObjectVal, field string, fnName string) (string, error) {
	v, ok := obj.Fields[field]
	if !ok || v.T != value.TypeString {
		return "", fmt.Errorf("%s: option %q must be string", fnName, field)
	}
	return v.AsString(), nil
}

func guiReadI32Opt(obj *value.ObjectVal, field string, fnName string) (int32, error) {
	v, ok := obj.Fields[field]
	if !ok || v.T != value.TypeI32 {
		return 0, fmt.Errorf("%s: option %q must be i32", fnName, field)
	}
	return v.AsI32(), nil
}

func guiReadBoolOpt(obj *value.ObjectVal, field string, fnName string) (bool, error) {
	v, ok := obj.Fields[field]
	if !ok || v.T != value.TypeBool {
		return false, fmt.Errorf("%s: option %q must be bool", fnName, field)
	}
	return v.AsBool(), nil
}

func guiReadOptionalCallback(obj *value.ObjectVal, field string, fnName string) (value.Value, bool, error) {
	v, ok := obj.Fields[field]
	if !ok || v.T == value.TypeVoid {
		return value.Void, false, nil
	}
	if v.T != value.TypeFunc && v.T != value.TypeNativeFunc {
		return value.Void, false, fmt.Errorf("%s: option %q must be fn", fnName, field)
	}
	return v, true, nil
}

func (interp *Interpreter) makeDefaultAppOpts() value.Value {
	return newGuiOptsObject(guiClassAppOpts, nil)
}

func (interp *Interpreter) makeDefaultWindowOpts(title string) value.Value {
	return newGuiOptsObject(guiClassWindowOpts, map[string]value.Value{
		"title":  value.NewString(title),
		"width":  value.NewI32(800),
		"height": value.NewI32(600),
	})
}

func (interp *Interpreter) makeDefaultBoxOpts(vertical bool) value.Value {
	return newGuiOptsObject(guiClassBoxOpts, map[string]value.Value{
		"vertical": value.NewBool(vertical),
		"spacing":  value.NewI32(8),
		"padding":  value.NewI32(12),
	})
}

func (interp *Interpreter) makeDefaultLabelOpts(text string) value.Value {
	return newGuiOptsObject(guiClassLabelOpts, map[string]value.Value{
		"text": value.NewString(text),
	})
}

func (interp *Interpreter) makeDefaultButtonOpts(text string) value.Value {
	return newGuiOptsObject(guiClassButtonOpts, map[string]value.Value{
		"text":     value.NewString(text),
		"width":    value.NewI32(0),
		"height":   value.NewI32(0),
		"on_click": value.Void,
	})
}

func (interp *Interpreter) makeDefaultEntryOpts() value.Value {
	return newGuiOptsObject(guiClassEntryOpts, map[string]value.Value{
		"text":  value.NewString(""),
		"width": value.NewI32(240),
	})
}

func (interp *Interpreter) makeGuiModule() *Env {
	env := NewEnv(nil)

	env.Define("supported", value.NewNativeFunc("gui.supported", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.supported: expected 0 arguments")
		}
		return value.NewBool(guiSupported()), nil
	}))

	env.Define("backend", value.NewNativeFunc("gui.backend", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.backend: expected 0 arguments")
		}
		return value.NewString(guiBackendName()), nil
	}))

	env.Define("app_opts", value.NewNativeFunc("gui.app_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.app_opts: expected 0 arguments")
		}
		return interp.makeDefaultAppOpts(), nil
	}))

	env.Define("window_opts", value.NewNativeFunc("gui.window_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("gui.window_opts: expected string title")
		}
		return interp.makeDefaultWindowOpts(args[0].AsString()), nil
	}))

	env.Define("box_opts", value.NewNativeFunc("gui.box_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.box_opts: expected 0 arguments")
		}
		return interp.makeDefaultBoxOpts(true), nil
	}))

	env.Define("label_opts", value.NewNativeFunc("gui.label_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("gui.label_opts: expected string text")
		}
		return interp.makeDefaultLabelOpts(args[0].AsString()), nil
	}))

	env.Define("button_opts", value.NewNativeFunc("gui.button_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("gui.button_opts: expected string text")
		}
		return interp.makeDefaultButtonOpts(args[0].AsString()), nil
	}))

	env.Define("entry_opts", value.NewNativeFunc("gui.entry_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.entry_opts: expected 0 arguments")
		}
		return interp.makeDefaultEntryOpts(), nil
	}))

	env.Define("app", value.NewNativeFunc("gui.app", func(args []value.Value) (value.Value, error) {
		if _, err := guiExpectOpts(args, guiClassAppOpts, "gui.app"); err != nil {
			return value.Void, err
		}
		handle, err := guiAppCreate()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiApp(handle), value.Ok}}
	}))

	env.Define("box", value.NewNativeFunc("gui.box", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassBoxOpts, "gui.box")
		if err != nil {
			return value.Void, err
		}
		vertical, err := guiReadBoolOpt(opts, "vertical", "gui.box")
		if err != nil {
			return value.Void, err
		}
		spacing, err := guiReadI32Opt(opts, "spacing", "gui.box")
		if err != nil {
			return value.Void, err
		}
		padding, err := guiReadI32Opt(opts, "padding", "gui.box")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiBoxCreate(vertical)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if spacing >= 0 {
			if err := guiBoxSetSpacing(handle, spacing); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		if padding >= 0 {
			if err := guiBoxSetPadding(handle, padding); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiBox(handle), value.Ok}}
	}))

	env.Define("vbox", value.NewNativeFunc("gui.vbox", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.vbox: expected 0 arguments")
		}
		boxOpts := interp.makeDefaultBoxOpts(true)
		boxCtor, ok := env.Get("box")
		if !ok {
			return value.Void, fmt.Errorf("gui.vbox: box constructor unavailable")
		}
		return boxCtor.AsNativeFunc().Fn([]value.Value{boxOpts})
	}))

	env.Define("hbox", value.NewNativeFunc("gui.hbox", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.hbox: expected 0 arguments")
		}
		boxOpts := interp.makeDefaultBoxOpts(false)
		boxCtor, ok := env.Get("box")
		if !ok {
			return value.Void, fmt.Errorf("gui.hbox: box constructor unavailable")
		}
		return boxCtor.AsNativeFunc().Fn([]value.Value{boxOpts})
	}))

	env.Define("label", value.NewNativeFunc("gui.label", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassLabelOpts, "gui.label")
		if err != nil {
			return value.Void, err
		}
		text, err := guiReadStringOpt(opts, "text", "gui.label")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiLabelCreate(text)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiLabel(handle), value.Ok}}
	}))

	env.Define("button", value.NewNativeFunc("gui.button", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassButtonOpts, "gui.button")
		if err != nil {
			return value.Void, err
		}
		text, err := guiReadStringOpt(opts, "text", "gui.button")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.button")
		if err != nil {
			return value.Void, err
		}
		height, err := guiReadI32Opt(opts, "height", "gui.button")
		if err != nil {
			return value.Void, err
		}
		clickFn, hasClickFn, err := guiReadOptionalCallback(opts, "on_click", "gui.button")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiButtonCreate(text)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 || height > 0 {
			if err := guiWidgetSetSize(handle, width, height); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		btn := interp.newGuiButton(handle)
		if hasClickFn {
			cbID := registerGuiCallback(interp, clickFn)
			if err := guiButtonSetOnClick(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{btn, value.Ok}}
	}))

	env.Define("entry", value.NewNativeFunc("gui.entry", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassEntryOpts, "gui.entry")
		if err != nil {
			return value.Void, err
		}
		text, err := guiReadStringOpt(opts, "text", "gui.entry")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.entry")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiEntryCreate()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 {
			if err := guiWidgetSetSize(handle, width, 0); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		if text != "" {
			if err := guiEntrySetText(handle, text); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiEntry(handle), value.Ok}}
	}))

	return env
}

func (interp *Interpreter) newGuiApp(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"run": value.NewNativeFunc("gui.App.run", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("App.run: expected 0 arguments")
			}
			if err := guiAppRun(handle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"quit": value.NewNativeFunc("gui.App.quit", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("App.quit: expected 0 arguments")
			}
			if err := guiAppQuit(handle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"window": value.NewNativeFunc("gui.App.window", func(args []value.Value) (value.Value, error) {
			opts, err := guiExpectOpts(args, guiClassWindowOpts, "App.window")
			if err != nil {
				return value.Void, err
			}
			title, err := guiReadStringOpt(opts, "title", "App.window")
			if err != nil {
				return value.Void, err
			}
			width, err := guiReadI32Opt(opts, "width", "App.window")
			if err != nil {
				return value.Void, err
			}
			height, err := guiReadI32Opt(opts, "height", "App.window")
			if err != nil {
				return value.Void, err
			}
			windowHandle, err := guiWindowCreate(title, width, height)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiWindow(windowHandle), value.Ok}}
		}),
	}
	return newGuiObject(guiClassApp, handle, methods)
}

func (interp *Interpreter) newGuiWindow(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"set_child": value.NewNativeFunc("gui.Window.set_child", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Window.set_child: expected 1 argument")
			}
			viewHandle, err := guiWidgetHandle(args[0])
			if err != nil {
				return guiStateErr(err), nil
			}
			if err := guiWindowSetChild(handle, viewHandle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"set_title": value.NewNativeFunc("gui.Window.set_title", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Window.set_title: expected string title")
			}
			if err := guiWindowSetTitle(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"show": value.NewNativeFunc("gui.Window.show", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Window.show: expected 0 arguments")
			}
			if err := guiWindowShow(handle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"close": value.NewNativeFunc("gui.Window.close", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Window.close: expected 0 arguments")
			}
			if err := guiWindowClose(handle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassWindow, handle, methods)
}

func (interp *Interpreter) newGuiBox(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"add": value.NewNativeFunc("gui.Box.add", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Box.add: expected 1 argument")
			}
			childHandle, err := guiWidgetHandle(args[0])
			if err != nil {
				return guiStateErr(err), nil
			}
			if err := guiBoxAdd(handle, childHandle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassBox, handle, methods)
}

func (interp *Interpreter) newGuiLabel(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"set_text": value.NewNativeFunc("gui.Label.set_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Label.set_text: expected string text")
			}
			if err := guiLabelSetText(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassLabel, handle, methods)
}

func (interp *Interpreter) newGuiButton(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"set_text": value.NewNativeFunc("gui.Button.set_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Button.set_text: expected string text")
			}
			if err := guiButtonSetText(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_click": value.NewNativeFunc("gui.Button.on_click", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Button.on_click: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Button.on_click: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiButtonSetOnClick(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassButton, handle, methods)
}

func (interp *Interpreter) newGuiEntry(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"text": value.NewNativeFunc("gui.Entry.text", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Entry.text: expected 0 arguments")
			}
			text, err := guiEntryText(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(text), value.Ok}}
		}),
		"set_text": value.NewNativeFunc("gui.Entry.set_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Entry.set_text: expected string text")
			}
			if err := guiEntrySetText(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassEntry, handle, methods)
}
