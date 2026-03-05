package interp

import (
	"fmt"
	"sync"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

const (
	guiClassApp       = "gui.App"
	guiClassWindow    = "gui.Window"
	guiClassBox       = "gui.Box"
	guiClassGrid      = "gui.Grid"
	guiClassLabel     = "gui.Label"
	guiClassButton    = "gui.Button"
	guiClassEntry     = "gui.Entry"
	guiClassCheckbox  = "gui.Checkbox"
	guiClassSelect    = "gui.Select"
	guiClassTextArea  = "gui.TextArea"
	guiClassProgress  = "gui.Progress"
	guiClassFrame     = "gui.Frame"
	guiClassGroup     = "gui.Group"
	guiClassRadio     = "gui.Radio"
	guiClassScale     = "gui.Scale"
	guiClassSpinbox   = "gui.Spinbox"
	guiClassSeparator = "gui.Separator"
	guiClassTabs      = "gui.Tabs"
	guiClassPaned     = "gui.Paned"
	guiClassList      = "gui.List"
	guiClassTree      = "gui.Tree"

	guiClassAppOpts       = "gui.AppOpts"
	guiClassWindowOpts    = "gui.WindowOpts"
	guiClassBoxOpts       = "gui.BoxOpts"
	guiClassGridOpts      = "gui.GridOpts"
	guiClassCellOpts      = "gui.CellOpts"
	guiClassLabelOpts     = "gui.LabelOpts"
	guiClassButtonOpts    = "gui.ButtonOpts"
	guiClassEntryOpts     = "gui.EntryOpts"
	guiClassCheckboxOpts  = "gui.CheckboxOpts"
	guiClassSelectOpts    = "gui.SelectOpts"
	guiClassTextAreaOpts  = "gui.TextAreaOpts"
	guiClassProgressOpts  = "gui.ProgressOpts"
	guiClassFrameOpts     = "gui.FrameOpts"
	guiClassGroupOpts     = "gui.GroupOpts"
	guiClassRadioOpts     = "gui.RadioOpts"
	guiClassScaleOpts     = "gui.ScaleOpts"
	guiClassSpinboxOpts   = "gui.SpinboxOpts"
	guiClassSeparatorOpts = "gui.SeparatorOpts"
	guiClassTabsOpts      = "gui.TabsOpts"
	guiClassPanedOpts     = "gui.PanedOpts"
	guiClassListOpts      = "gui.ListOpts"
	guiClassTreeOpts      = "gui.TreeOpts"
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
	case guiClassBox, guiClassGrid, guiClassLabel, guiClassButton, guiClassEntry, guiClassCheckbox, guiClassSelect, guiClassTextArea, guiClassProgress, guiClassFrame, guiClassGroup, guiClassRadio, guiClassScale, guiClassSpinbox, guiClassSeparator, guiClassTabs, guiClassPaned, guiClassList, guiClassTree:
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

func guiReadF64Opt(obj *value.ObjectVal, field string, fnName string) (float64, error) {
	v, ok := obj.Fields[field]
	if !ok || v.T != value.TypeF64 {
		return 0, fmt.Errorf("%s: option %q must be f64", fnName, field)
	}
	return v.AsF64(), nil
}

func guiReadStringArrayOpt(obj *value.ObjectVal, field string, fnName string) ([]string, error) {
	v, ok := obj.Fields[field]
	if !ok || v.T != value.TypeArray {
		return nil, fmt.Errorf("%s: option %q must be array<string>", fnName, field)
	}
	arr := v.AsArray().Elems
	out := make([]string, 0, len(arr))
	for i, elem := range arr {
		if elem.T != value.TypeString {
			return nil, fmt.Errorf("%s: option %q item %d must be string", fnName, field, i)
		}
		out = append(out, elem.AsString())
	}
	return out, nil
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

func (interp *Interpreter) makeDefaultGridOpts() value.Value {
	return newGuiOptsObject(guiClassGridOpts, map[string]value.Value{
		"row_spacing": value.NewI32(8),
		"col_spacing": value.NewI32(8),
	})
}

func (interp *Interpreter) makeDefaultCellOpts(row int32, col int32) value.Value {
	return newGuiOptsObject(guiClassCellOpts, map[string]value.Value{
		"row":      value.NewI32(row),
		"col":      value.NewI32(col),
		"row_span": value.NewI32(1),
		"col_span": value.NewI32(1),
		"fill_x":   value.NewBool(true),
		"fill_y":   value.NewBool(false),
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

func (interp *Interpreter) makeDefaultCheckboxOpts(text string) value.Value {
	return newGuiOptsObject(guiClassCheckboxOpts, map[string]value.Value{
		"text":      value.NewString(text),
		"checked":   value.NewBool(false),
		"on_toggle": value.Void,
	})
}

func (interp *Interpreter) makeDefaultSelectOpts() value.Value {
	return newGuiOptsObject(guiClassSelectOpts, map[string]value.Value{
		"options":   value.NewArray(nil),
		"selected":  value.NewI32(0),
		"width":     value.NewI32(240),
		"on_change": value.Void,
	})
}

func (interp *Interpreter) makeDefaultTextAreaOpts() value.Value {
	return newGuiOptsObject(guiClassTextAreaOpts, map[string]value.Value{
		"text":   value.NewString(""),
		"width":  value.NewI32(420),
		"height": value.NewI32(160),
	})
}

func (interp *Interpreter) makeDefaultProgressOpts() value.Value {
	return newGuiOptsObject(guiClassProgressOpts, map[string]value.Value{
		"min":           value.NewF64(0),
		"max":           value.NewF64(100),
		"value":         value.NewF64(0),
		"indeterminate": value.NewBool(false),
		"width":         value.NewI32(200),
	})
}

func (interp *Interpreter) makeDefaultFrameOpts() value.Value {
	return newGuiOptsObject(guiClassFrameOpts, map[string]value.Value{
		"padding": value.NewI32(8),
	})
}

func (interp *Interpreter) makeDefaultGroupOpts(title string) value.Value {
	return newGuiOptsObject(guiClassGroupOpts, map[string]value.Value{
		"title":   value.NewString(title),
		"padding": value.NewI32(10),
	})
}

func (interp *Interpreter) makeDefaultRadioOpts() value.Value {
	return newGuiOptsObject(guiClassRadioOpts, map[string]value.Value{
		"options":   value.NewArray(nil),
		"selected":  value.NewI32(0),
		"vertical":  value.NewBool(true),
		"on_change": value.Void,
	})
}

func (interp *Interpreter) makeDefaultScaleOpts() value.Value {
	return newGuiOptsObject(guiClassScaleOpts, map[string]value.Value{
		"min":       value.NewF64(0),
		"max":       value.NewF64(100),
		"value":     value.NewF64(0),
		"vertical":  value.NewBool(false),
		"width":     value.NewI32(220),
		"on_change": value.Void,
	})
}

func (interp *Interpreter) makeDefaultSpinboxOpts() value.Value {
	return newGuiOptsObject(guiClassSpinboxOpts, map[string]value.Value{
		"min":       value.NewF64(0),
		"max":       value.NewF64(100),
		"step":      value.NewF64(1),
		"value":     value.NewF64(0),
		"width":     value.NewI32(120),
		"on_change": value.Void,
	})
}

func (interp *Interpreter) makeDefaultSeparatorOpts() value.Value {
	return newGuiOptsObject(guiClassSeparatorOpts, map[string]value.Value{
		"vertical": value.NewBool(false),
		"length":   value.NewI32(160),
	})
}

func (interp *Interpreter) makeDefaultTabsOpts() value.Value {
	return newGuiOptsObject(guiClassTabsOpts, map[string]value.Value{
		"selected":  value.NewI32(0),
		"on_change": value.Void,
	})
}

func (interp *Interpreter) makeDefaultPanedOpts() value.Value {
	return newGuiOptsObject(guiClassPanedOpts, map[string]value.Value{
		"vertical":  value.NewBool(false),
		"ratio":     value.NewF64(0.5),
		"on_change": value.Void,
	})
}

func (interp *Interpreter) makeDefaultListOpts() value.Value {
	return newGuiOptsObject(guiClassListOpts, map[string]value.Value{
		"items":     value.NewArray(nil),
		"selected":  value.NewI32(0),
		"width":     value.NewI32(280),
		"height":    value.NewI32(180),
		"on_change": value.Void,
	})
}

func (interp *Interpreter) makeDefaultTreeOpts() value.Value {
	return newGuiOptsObject(guiClassTreeOpts, map[string]value.Value{
		"width":     value.NewI32(320),
		"height":    value.NewI32(220),
		"on_change": value.Void,
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

	env.Define("grid_opts", value.NewNativeFunc("gui.grid_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.grid_opts: expected 0 arguments")
		}
		return interp.makeDefaultGridOpts(), nil
	}))

	env.Define("cell_opts", value.NewNativeFunc("gui.cell_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeI32 || args[1].T != value.TypeI32 {
			return value.Void, fmt.Errorf("gui.cell_opts: expected i32 row, i32 col")
		}
		return interp.makeDefaultCellOpts(args[0].AsI32(), args[1].AsI32()), nil
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

	env.Define("checkbox_opts", value.NewNativeFunc("gui.checkbox_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("gui.checkbox_opts: expected string text")
		}
		return interp.makeDefaultCheckboxOpts(args[0].AsString()), nil
	}))

	env.Define("select_opts", value.NewNativeFunc("gui.select_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.select_opts: expected 0 arguments")
		}
		return interp.makeDefaultSelectOpts(), nil
	}))

	env.Define("textarea_opts", value.NewNativeFunc("gui.textarea_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.textarea_opts: expected 0 arguments")
		}
		return interp.makeDefaultTextAreaOpts(), nil
	}))

	env.Define("progress_opts", value.NewNativeFunc("gui.progress_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.progress_opts: expected 0 arguments")
		}
		return interp.makeDefaultProgressOpts(), nil
	}))

	env.Define("frame_opts", value.NewNativeFunc("gui.frame_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.frame_opts: expected 0 arguments")
		}
		return interp.makeDefaultFrameOpts(), nil
	}))

	env.Define("group_opts", value.NewNativeFunc("gui.group_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("gui.group_opts: expected string title")
		}
		return interp.makeDefaultGroupOpts(args[0].AsString()), nil
	}))

	env.Define("radio_opts", value.NewNativeFunc("gui.radio_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.radio_opts: expected 0 arguments")
		}
		return interp.makeDefaultRadioOpts(), nil
	}))

	env.Define("scale_opts", value.NewNativeFunc("gui.scale_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.scale_opts: expected 0 arguments")
		}
		return interp.makeDefaultScaleOpts(), nil
	}))

	env.Define("spinbox_opts", value.NewNativeFunc("gui.spinbox_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.spinbox_opts: expected 0 arguments")
		}
		return interp.makeDefaultSpinboxOpts(), nil
	}))

	env.Define("separator_opts", value.NewNativeFunc("gui.separator_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.separator_opts: expected 0 arguments")
		}
		return interp.makeDefaultSeparatorOpts(), nil
	}))

	env.Define("tabs_opts", value.NewNativeFunc("gui.tabs_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.tabs_opts: expected 0 arguments")
		}
		return interp.makeDefaultTabsOpts(), nil
	}))

	env.Define("paned_opts", value.NewNativeFunc("gui.paned_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.paned_opts: expected 0 arguments")
		}
		return interp.makeDefaultPanedOpts(), nil
	}))

	env.Define("list_opts", value.NewNativeFunc("gui.list_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.list_opts: expected 0 arguments")
		}
		return interp.makeDefaultListOpts(), nil
	}))

	env.Define("tree_opts", value.NewNativeFunc("gui.tree_opts", func(args []value.Value) (value.Value, error) {
		if len(args) != 0 {
			return value.Void, fmt.Errorf("gui.tree_opts: expected 0 arguments")
		}
		return interp.makeDefaultTreeOpts(), nil
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

	env.Define("grid", value.NewNativeFunc("gui.grid", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassGridOpts, "gui.grid")
		if err != nil {
			return value.Void, err
		}
		rowSpacing, err := guiReadI32Opt(opts, "row_spacing", "gui.grid")
		if err != nil {
			return value.Void, err
		}
		colSpacing, err := guiReadI32Opt(opts, "col_spacing", "gui.grid")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiGridCreate(rowSpacing, colSpacing)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiGrid(handle), value.Ok}}
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

	env.Define("checkbox", value.NewNativeFunc("gui.checkbox", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassCheckboxOpts, "gui.checkbox")
		if err != nil {
			return value.Void, err
		}
		text, err := guiReadStringOpt(opts, "text", "gui.checkbox")
		if err != nil {
			return value.Void, err
		}
		checked, err := guiReadBoolOpt(opts, "checked", "gui.checkbox")
		if err != nil {
			return value.Void, err
		}
		toggleFn, hasToggleFn, err := guiReadOptionalCallback(opts, "on_toggle", "gui.checkbox")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiCheckboxCreate(text, checked)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if hasToggleFn {
			cbID := registerGuiCallback(interp, toggleFn)
			if err := guiCheckboxSetOnToggle(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiCheckbox(handle), value.Ok}}
	}))

	env.Define("select", value.NewNativeFunc("gui.select", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassSelectOpts, "gui.select")
		if err != nil {
			return value.Void, err
		}
		options, err := guiReadStringArrayOpt(opts, "options", "gui.select")
		if err != nil {
			return value.Void, err
		}
		selected, err := guiReadI32Opt(opts, "selected", "gui.select")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.select")
		if err != nil {
			return value.Void, err
		}
		changeFn, hasChangeFn, err := guiReadOptionalCallback(opts, "on_change", "gui.select")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiSelectCreate(options, selected)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 {
			if err := guiWidgetSetSize(handle, width, 0); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		if hasChangeFn {
			cbID := registerGuiCallback(interp, changeFn)
			if err := guiSelectSetOnChange(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiSelect(handle), value.Ok}}
	}))

	env.Define("textarea", value.NewNativeFunc("gui.textarea", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassTextAreaOpts, "gui.textarea")
		if err != nil {
			return value.Void, err
		}
		text, err := guiReadStringOpt(opts, "text", "gui.textarea")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.textarea")
		if err != nil {
			return value.Void, err
		}
		height, err := guiReadI32Opt(opts, "height", "gui.textarea")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiTextAreaCreate()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 || height > 0 {
			if err := guiWidgetSetSize(handle, width, height); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		if text != "" {
			if err := guiTextAreaSetText(handle, text); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiTextArea(handle), value.Ok}}
	}))

	env.Define("progress", value.NewNativeFunc("gui.progress", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassProgressOpts, "gui.progress")
		if err != nil {
			return value.Void, err
		}
		min, err := guiReadF64Opt(opts, "min", "gui.progress")
		if err != nil {
			return value.Void, err
		}
		max, err := guiReadF64Opt(opts, "max", "gui.progress")
		if err != nil {
			return value.Void, err
		}
		current, err := guiReadF64Opt(opts, "value", "gui.progress")
		if err != nil {
			return value.Void, err
		}
		indeterminate, err := guiReadBoolOpt(opts, "indeterminate", "gui.progress")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.progress")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiProgressCreate(min, max, current, indeterminate)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 {
			if err := guiWidgetSetSize(handle, width, 0); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiProgress(handle), value.Ok}}
	}))

	env.Define("frame", value.NewNativeFunc("gui.frame", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassFrameOpts, "gui.frame")
		if err != nil {
			return value.Void, err
		}
		padding, err := guiReadI32Opt(opts, "padding", "gui.frame")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiFrameCreate(padding)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiFrame(handle), value.Ok}}
	}))

	env.Define("group", value.NewNativeFunc("gui.group", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassGroupOpts, "gui.group")
		if err != nil {
			return value.Void, err
		}
		title, err := guiReadStringOpt(opts, "title", "gui.group")
		if err != nil {
			return value.Void, err
		}
		padding, err := guiReadI32Opt(opts, "padding", "gui.group")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiGroupCreate(title, padding)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiGroup(handle), value.Ok}}
	}))

	env.Define("radio", value.NewNativeFunc("gui.radio", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassRadioOpts, "gui.radio")
		if err != nil {
			return value.Void, err
		}
		options, err := guiReadStringArrayOpt(opts, "options", "gui.radio")
		if err != nil {
			return value.Void, err
		}
		selected, err := guiReadI32Opt(opts, "selected", "gui.radio")
		if err != nil {
			return value.Void, err
		}
		vertical, err := guiReadBoolOpt(opts, "vertical", "gui.radio")
		if err != nil {
			return value.Void, err
		}
		changeFn, hasChangeFn, err := guiReadOptionalCallback(opts, "on_change", "gui.radio")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiRadioCreate(options, selected, vertical)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if hasChangeFn {
			cbID := registerGuiCallback(interp, changeFn)
			if err := guiRadioSetOnChange(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiRadio(handle), value.Ok}}
	}))

	env.Define("scale", value.NewNativeFunc("gui.scale", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassScaleOpts, "gui.scale")
		if err != nil {
			return value.Void, err
		}
		min, err := guiReadF64Opt(opts, "min", "gui.scale")
		if err != nil {
			return value.Void, err
		}
		max, err := guiReadF64Opt(opts, "max", "gui.scale")
		if err != nil {
			return value.Void, err
		}
		current, err := guiReadF64Opt(opts, "value", "gui.scale")
		if err != nil {
			return value.Void, err
		}
		vertical, err := guiReadBoolOpt(opts, "vertical", "gui.scale")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.scale")
		if err != nil {
			return value.Void, err
		}
		changeFn, hasChangeFn, err := guiReadOptionalCallback(opts, "on_change", "gui.scale")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiScaleCreate(min, max, current, vertical)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 {
			if vertical {
				if err := guiWidgetSetSize(handle, 0, width); err != nil {
					return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
				}
			} else {
				if err := guiWidgetSetSize(handle, width, 0); err != nil {
					return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
				}
			}
		}
		if hasChangeFn {
			cbID := registerGuiCallback(interp, changeFn)
			if err := guiScaleSetOnChange(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiScale(handle), value.Ok}}
	}))

	env.Define("spinbox", value.NewNativeFunc("gui.spinbox", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassSpinboxOpts, "gui.spinbox")
		if err != nil {
			return value.Void, err
		}
		min, err := guiReadF64Opt(opts, "min", "gui.spinbox")
		if err != nil {
			return value.Void, err
		}
		max, err := guiReadF64Opt(opts, "max", "gui.spinbox")
		if err != nil {
			return value.Void, err
		}
		step, err := guiReadF64Opt(opts, "step", "gui.spinbox")
		if err != nil {
			return value.Void, err
		}
		current, err := guiReadF64Opt(opts, "value", "gui.spinbox")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.spinbox")
		if err != nil {
			return value.Void, err
		}
		changeFn, hasChangeFn, err := guiReadOptionalCallback(opts, "on_change", "gui.spinbox")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiSpinboxCreate(min, max, step, current)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 {
			if err := guiWidgetSetSize(handle, width, 0); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		if hasChangeFn {
			cbID := registerGuiCallback(interp, changeFn)
			if err := guiSpinboxSetOnChange(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiSpinbox(handle), value.Ok}}
	}))

	env.Define("separator", value.NewNativeFunc("gui.separator", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassSeparatorOpts, "gui.separator")
		if err != nil {
			return value.Void, err
		}
		vertical, err := guiReadBoolOpt(opts, "vertical", "gui.separator")
		if err != nil {
			return value.Void, err
		}
		length, err := guiReadI32Opt(opts, "length", "gui.separator")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiSeparatorCreate(vertical)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if length > 0 {
			if vertical {
				if err := guiWidgetSetSize(handle, 1, length); err != nil {
					return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
				}
			} else {
				if err := guiWidgetSetSize(handle, length, 1); err != nil {
					return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
				}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiSeparator(handle), value.Ok}}
	}))

	env.Define("tabs", value.NewNativeFunc("gui.tabs", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassTabsOpts, "gui.tabs")
		if err != nil {
			return value.Void, err
		}
		selected, err := guiReadI32Opt(opts, "selected", "gui.tabs")
		if err != nil {
			return value.Void, err
		}
		changeFn, hasChangeFn, err := guiReadOptionalCallback(opts, "on_change", "gui.tabs")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiTabsCreate(selected)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if hasChangeFn {
			cbID := registerGuiCallback(interp, changeFn)
			if err := guiTabsSetOnChange(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiTabs(handle), value.Ok}}
	}))

	env.Define("paned", value.NewNativeFunc("gui.paned", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassPanedOpts, "gui.paned")
		if err != nil {
			return value.Void, err
		}
		vertical, err := guiReadBoolOpt(opts, "vertical", "gui.paned")
		if err != nil {
			return value.Void, err
		}
		ratio, err := guiReadF64Opt(opts, "ratio", "gui.paned")
		if err != nil {
			return value.Void, err
		}
		changeFn, hasChangeFn, err := guiReadOptionalCallback(opts, "on_change", "gui.paned")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiPanedCreate(vertical, ratio)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if hasChangeFn {
			cbID := registerGuiCallback(interp, changeFn)
			if err := guiPanedSetOnChange(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiPaned(handle), value.Ok}}
	}))

	env.Define("list", value.NewNativeFunc("gui.list", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassListOpts, "gui.list")
		if err != nil {
			return value.Void, err
		}
		items, err := guiReadStringArrayOpt(opts, "items", "gui.list")
		if err != nil {
			return value.Void, err
		}
		selected, err := guiReadI32Opt(opts, "selected", "gui.list")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.list")
		if err != nil {
			return value.Void, err
		}
		height, err := guiReadI32Opt(opts, "height", "gui.list")
		if err != nil {
			return value.Void, err
		}
		changeFn, hasChangeFn, err := guiReadOptionalCallback(opts, "on_change", "gui.list")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiListCreate(items, selected)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 || height > 0 {
			if err := guiWidgetSetSize(handle, width, height); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		if hasChangeFn {
			cbID := registerGuiCallback(interp, changeFn)
			if err := guiListSetOnChange(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiList(handle), value.Ok}}
	}))

	env.Define("tree", value.NewNativeFunc("gui.tree", func(args []value.Value) (value.Value, error) {
		opts, err := guiExpectOpts(args, guiClassTreeOpts, "gui.tree")
		if err != nil {
			return value.Void, err
		}
		width, err := guiReadI32Opt(opts, "width", "gui.tree")
		if err != nil {
			return value.Void, err
		}
		height, err := guiReadI32Opt(opts, "height", "gui.tree")
		if err != nil {
			return value.Void, err
		}
		changeFn, hasChangeFn, err := guiReadOptionalCallback(opts, "on_change", "gui.tree")
		if err != nil {
			return value.Void, err
		}
		handle, err := guiTreeCreate()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
		}
		if width > 0 || height > 0 {
			if err := guiWidgetSetSize(handle, width, height); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		if hasChangeFn {
			cbID := registerGuiCallback(interp, changeFn)
			if err := guiTreeSetOnChange(handle, cbID); err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, guiStateErr(err)}}
			}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.newGuiTree(handle), value.Ok}}
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

func (interp *Interpreter) newGuiGrid(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"place": value.NewNativeFunc("gui.Grid.place", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 {
				return value.Void, fmt.Errorf("Grid.place: expected widget and gui.CellOpts")
			}
			childHandle, err := guiWidgetHandle(args[0])
			if err != nil {
				return guiStateErr(err), nil
			}
			cellOpts, err := guiExpectOpts(args[1:], guiClassCellOpts, "Grid.place")
			if err != nil {
				return value.Void, err
			}
			row, err := guiReadI32Opt(cellOpts, "row", "Grid.place")
			if err != nil {
				return value.Void, err
			}
			col, err := guiReadI32Opt(cellOpts, "col", "Grid.place")
			if err != nil {
				return value.Void, err
			}
			rowSpan, err := guiReadI32Opt(cellOpts, "row_span", "Grid.place")
			if err != nil {
				return value.Void, err
			}
			colSpan, err := guiReadI32Opt(cellOpts, "col_span", "Grid.place")
			if err != nil {
				return value.Void, err
			}
			fillX, err := guiReadBoolOpt(cellOpts, "fill_x", "Grid.place")
			if err != nil {
				return value.Void, err
			}
			fillY, err := guiReadBoolOpt(cellOpts, "fill_y", "Grid.place")
			if err != nil {
				return value.Void, err
			}
			if row < 0 || col < 0 {
				return guiStateErr(fmt.Errorf("grid cell row/col must be >= 0")), nil
			}
			if rowSpan <= 0 || colSpan <= 0 {
				return guiStateErr(fmt.Errorf("grid cell spans must be > 0")), nil
			}
			if err := guiGridPlace(handle, childHandle, row, col, rowSpan, colSpan, fillX, fillY); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassGrid, handle, methods)
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

func (interp *Interpreter) newGuiCheckbox(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"checked": value.NewNativeFunc("gui.Checkbox.checked", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Checkbox.checked: expected 0 arguments")
			}
			checked, err := guiCheckboxChecked(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewBool(false), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewBool(checked), value.Ok}}
		}),
		"set_checked": value.NewNativeFunc("gui.Checkbox.set_checked", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeBool {
				return value.Void, fmt.Errorf("Checkbox.set_checked: expected bool")
			}
			if err := guiCheckboxSetChecked(handle, args[0].AsBool()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"set_text": value.NewNativeFunc("gui.Checkbox.set_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Checkbox.set_text: expected string text")
			}
			if err := guiCheckboxSetText(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_toggle": value.NewNativeFunc("gui.Checkbox.on_toggle", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Checkbox.on_toggle: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Checkbox.on_toggle: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiCheckboxSetOnToggle(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassCheckbox, handle, methods)
}

func (interp *Interpreter) newGuiSelect(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"selected_index": value.NewNativeFunc("gui.Select.selected_index", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Select.selected_index: expected 0 arguments")
			}
			index, err := guiSelectSelectedIndex(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(0), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(index), value.Ok}}
		}),
		"set_selected_index": value.NewNativeFunc("gui.Select.set_selected_index", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("Select.set_selected_index: expected i32 index")
			}
			if err := guiSelectSetSelectedIndex(handle, args[0].AsI32()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"selected_text": value.NewNativeFunc("gui.Select.selected_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Select.selected_text: expected 0 arguments")
			}
			text, err := guiSelectSelectedText(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(text), value.Ok}}
		}),
		"add_item": value.NewNativeFunc("gui.Select.add_item", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Select.add_item: expected string item")
			}
			if err := guiSelectAddItem(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_change": value.NewNativeFunc("gui.Select.on_change", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Select.on_change: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Select.on_change: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiSelectSetOnChange(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassSelect, handle, methods)
}

func (interp *Interpreter) newGuiTextArea(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"text": value.NewNativeFunc("gui.TextArea.text", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("TextArea.text: expected 0 arguments")
			}
			text, err := guiTextAreaText(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(text), value.Ok}}
		}),
		"set_text": value.NewNativeFunc("gui.TextArea.set_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("TextArea.set_text: expected string text")
			}
			if err := guiTextAreaSetText(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"append": value.NewNativeFunc("gui.TextArea.append", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("TextArea.append: expected string text")
			}
			if err := guiTextAreaAppend(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassTextArea, handle, methods)
}

func (interp *Interpreter) newGuiProgress(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"value": value.NewNativeFunc("gui.Progress.value", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Progress.value: expected 0 arguments")
			}
			current, err := guiProgressValue(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(0), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(current), value.Ok}}
		}),
		"set_value": value.NewNativeFunc("gui.Progress.set_value", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeF64 {
				return value.Void, fmt.Errorf("Progress.set_value: expected f64")
			}
			if err := guiProgressSetValue(handle, args[0].AsF64()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassProgress, handle, methods)
}

func (interp *Interpreter) newGuiFrame(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"set_child": value.NewNativeFunc("gui.Frame.set_child", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Frame.set_child: expected 1 argument")
			}
			childHandle, err := guiWidgetHandle(args[0])
			if err != nil {
				return guiStateErr(err), nil
			}
			if err := guiFrameSetChild(handle, childHandle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassFrame, handle, methods)
}

func (interp *Interpreter) newGuiGroup(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"set_child": value.NewNativeFunc("gui.Group.set_child", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Group.set_child: expected 1 argument")
			}
			childHandle, err := guiWidgetHandle(args[0])
			if err != nil {
				return guiStateErr(err), nil
			}
			if err := guiGroupSetChild(handle, childHandle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"set_title": value.NewNativeFunc("gui.Group.set_title", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Group.set_title: expected string title")
			}
			if err := guiGroupSetTitle(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassGroup, handle, methods)
}

func (interp *Interpreter) newGuiRadio(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"selected_index": value.NewNativeFunc("gui.Radio.selected_index", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Radio.selected_index: expected 0 arguments")
			}
			index, err := guiRadioSelectedIndex(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(-1), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(index), value.Ok}}
		}),
		"set_selected_index": value.NewNativeFunc("gui.Radio.set_selected_index", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("Radio.set_selected_index: expected i32")
			}
			if err := guiRadioSetSelectedIndex(handle, args[0].AsI32()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"selected_text": value.NewNativeFunc("gui.Radio.selected_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Radio.selected_text: expected 0 arguments")
			}
			text, err := guiRadioSelectedText(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(text), value.Ok}}
		}),
		"on_change": value.NewNativeFunc("gui.Radio.on_change", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Radio.on_change: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Radio.on_change: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiRadioSetOnChange(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassRadio, handle, methods)
}

func (interp *Interpreter) newGuiScale(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"value": value.NewNativeFunc("gui.Scale.value", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Scale.value: expected 0 arguments")
			}
			current, err := guiScaleValue(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(0), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(current), value.Ok}}
		}),
		"set_value": value.NewNativeFunc("gui.Scale.set_value", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeF64 {
				return value.Void, fmt.Errorf("Scale.set_value: expected f64")
			}
			if err := guiScaleSetValue(handle, args[0].AsF64()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_change": value.NewNativeFunc("gui.Scale.on_change", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Scale.on_change: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Scale.on_change: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiScaleSetOnChange(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassScale, handle, methods)
}

func (interp *Interpreter) newGuiSpinbox(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"value": value.NewNativeFunc("gui.Spinbox.value", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Spinbox.value: expected 0 arguments")
			}
			current, err := guiSpinboxValue(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(0), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(current), value.Ok}}
		}),
		"set_value": value.NewNativeFunc("gui.Spinbox.set_value", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeF64 {
				return value.Void, fmt.Errorf("Spinbox.set_value: expected f64")
			}
			if err := guiSpinboxSetValue(handle, args[0].AsF64()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_change": value.NewNativeFunc("gui.Spinbox.on_change", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Spinbox.on_change: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Spinbox.on_change: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiSpinboxSetOnChange(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassSpinbox, handle, methods)
}

func (interp *Interpreter) newGuiSeparator(handle uintptr) value.Value {
	return newGuiObject(guiClassSeparator, handle, map[string]value.Value{})
}

func (interp *Interpreter) newGuiTabs(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"add_tab": value.NewNativeFunc("gui.Tabs.add_tab", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Tabs.add_tab: expected string title, widget")
			}
			childHandle, err := guiWidgetHandle(args[1])
			if err != nil {
				return guiStateErr(err), nil
			}
			if err := guiTabsAddTab(handle, args[0].AsString(), childHandle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"selected_index": value.NewNativeFunc("gui.Tabs.selected_index", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Tabs.selected_index: expected 0 arguments")
			}
			index, err := guiTabsSelectedIndex(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(-1), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(index), value.Ok}}
		}),
		"set_selected_index": value.NewNativeFunc("gui.Tabs.set_selected_index", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("Tabs.set_selected_index: expected i32")
			}
			if err := guiTabsSetSelectedIndex(handle, args[0].AsI32()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_change": value.NewNativeFunc("gui.Tabs.on_change", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Tabs.on_change: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Tabs.on_change: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiTabsSetOnChange(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassTabs, handle, methods)
}

func (interp *Interpreter) newGuiPaned(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"set_first": value.NewNativeFunc("gui.Paned.set_first", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Paned.set_first: expected widget")
			}
			childHandle, err := guiWidgetHandle(args[0])
			if err != nil {
				return guiStateErr(err), nil
			}
			if err := guiPanedSetFirst(handle, childHandle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"set_second": value.NewNativeFunc("gui.Paned.set_second", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Paned.set_second: expected widget")
			}
			childHandle, err := guiWidgetHandle(args[0])
			if err != nil {
				return guiStateErr(err), nil
			}
			if err := guiPanedSetSecond(handle, childHandle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"ratio": value.NewNativeFunc("gui.Paned.ratio", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Paned.ratio: expected 0 arguments")
			}
			ratio, err := guiPanedRatio(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(0.5), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(ratio), value.Ok}}
		}),
		"set_ratio": value.NewNativeFunc("gui.Paned.set_ratio", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeF64 {
				return value.Void, fmt.Errorf("Paned.set_ratio: expected f64")
			}
			if err := guiPanedSetRatio(handle, args[0].AsF64()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_change": value.NewNativeFunc("gui.Paned.on_change", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Paned.on_change: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Paned.on_change: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiPanedSetOnChange(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassPaned, handle, methods)
}

func (interp *Interpreter) newGuiList(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"selected_index": value.NewNativeFunc("gui.List.selected_index", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("List.selected_index: expected 0 arguments")
			}
			index, err := guiListSelectedIndex(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(-1), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(index), value.Ok}}
		}),
		"set_selected_index": value.NewNativeFunc("gui.List.set_selected_index", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("List.set_selected_index: expected i32")
			}
			if err := guiListSetSelectedIndex(handle, args[0].AsI32()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"selected_text": value.NewNativeFunc("gui.List.selected_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("List.selected_text: expected 0 arguments")
			}
			text, err := guiListSelectedText(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(text), value.Ok}}
		}),
		"add_item": value.NewNativeFunc("gui.List.add_item", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("List.add_item: expected string item")
			}
			if err := guiListAddItem(handle, args[0].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"clear": value.NewNativeFunc("gui.List.clear", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("List.clear: expected 0 arguments")
			}
			if err := guiListClear(handle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_change": value.NewNativeFunc("gui.List.on_change", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("List.on_change: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("List.on_change: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiListSetOnChange(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassList, handle, methods)
}

func (interp *Interpreter) newGuiTree(handle uintptr) value.Value {
	methods := map[string]value.Value{
		"add_root": value.NewNativeFunc("gui.Tree.add_root", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Tree.add_root: expected string title")
			}
			nodeID, err := guiTreeAddRoot(handle, args[0].AsString())
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(-1), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(nodeID), value.Ok}}
		}),
		"add_child": value.NewNativeFunc("gui.Tree.add_child", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 || args[0].T != value.TypeI32 || args[1].T != value.TypeString {
				return value.Void, fmt.Errorf("Tree.add_child: expected i32 parent_id, string title")
			}
			nodeID, err := guiTreeAddChild(handle, args[0].AsI32(), args[1].AsString())
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(-1), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(nodeID), value.Ok}}
		}),
		"set_text": value.NewNativeFunc("gui.Tree.set_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 || args[0].T != value.TypeI32 || args[1].T != value.TypeString {
				return value.Void, fmt.Errorf("Tree.set_text: expected i32 node_id, string title")
			}
			if err := guiTreeSetText(handle, args[0].AsI32(), args[1].AsString()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"selected_id": value.NewNativeFunc("gui.Tree.selected_id", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Tree.selected_id: expected 0 arguments")
			}
			nodeID, err := guiTreeSelectedID(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(-1), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(nodeID), value.Ok}}
		}),
		"set_selected_id": value.NewNativeFunc("gui.Tree.set_selected_id", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("Tree.set_selected_id: expected i32 node_id")
			}
			if err := guiTreeSetSelectedID(handle, args[0].AsI32()); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"selected_text": value.NewNativeFunc("gui.Tree.selected_text", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Tree.selected_text: expected 0 arguments")
			}
			text, err := guiTreeSelectedText(handle)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), guiStateErr(err)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(text), value.Ok}}
		}),
		"clear": value.NewNativeFunc("gui.Tree.clear", func(args []value.Value) (value.Value, error) {
			if len(args) != 0 {
				return value.Void, fmt.Errorf("Tree.clear: expected 0 arguments")
			}
			if err := guiTreeClear(handle); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
		"on_change": value.NewNativeFunc("gui.Tree.on_change", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("Tree.on_change: expected fn callback")
			}
			if args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc {
				return value.Void, fmt.Errorf("Tree.on_change: expected fn callback")
			}
			cbID := registerGuiCallback(interp, args[0])
			if err := guiTreeSetOnChange(handle, cbID); err != nil {
				return guiStateErr(err), nil
			}
			return value.Ok, nil
		}),
	}
	return newGuiObject(guiClassTree, handle, methods)
}
