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

func guiObjectHandle(v value.Value, className string) (uintptr, error) {
	if v.T != value.TypeObject {
		return 0, fmt.Errorf("expected %s object", className)
	}
	obj := v.AsObject()
	if obj.ClassName != className {
		return 0, fmt.Errorf("expected %s object, got %s", className, obj.ClassName)
	}
	handle, ok := obj.Fields["__handle"]
	if !ok || handle.T != value.TypePtr {
		return 0, fmt.Errorf("invalid %s handle", className)
	}
	return handle.AsPtr(), nil
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

	env.Define("app", value.NewNativeFunc("gui.app", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.app: expected 0 arguments")
		}
		handle, err := guiAppCreate()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiApp(handle), value.Ok}}
	}))

	env.Define("vbox", value.NewNativeFunc("gui.vbox", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.vbox: expected 0 arguments")
		}
		handle, err := guiBoxCreate(true)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiBox(handle), value.Ok}}
	}))

	env.Define("hbox", value.NewNativeFunc("gui.hbox", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.hbox: expected 0 arguments")
		}
		handle, err := guiBoxCreate(false)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiBox(handle), value.Ok}}
	}))

	env.Define("label", value.NewNativeFunc("gui.label", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("gui.label: expected string text")
		}
		handle, err := guiLabelCreate(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiLabel(handle), value.Ok}}
	}))

	env.Define("button", value.NewNativeFunc("gui.button", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("gui.button: expected string text")
		}
		handle, err := guiButtonCreate(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiButton(handle), value.Ok}}
	}))

	env.Define("entry", value.NewNativeFunc("gui.entry", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.entry: expected 0 arguments")
		}
		handle, err := guiEntryCreate()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
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
			if len(args) != 3 || args[0].T != value.TypeString || args[1].T != value.TypeI32 || args[2].T != value.TypeI32 {
				return value.Void, fmt.Errorf("App.window: expected (string title, i32 width, i32 height)")
			}
			windowHandle, err := guiWindowCreate(args[0].AsString(), args[1].AsI32(), args[2].AsI32())
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
