package interp

import (
	"github.com/bluesentinelsec/basl/pkg/basl/value"
	gui "github.com/gen2brain/raylib-go/raygui"
	rl "github.com/gen2brain/raylib-go/raylib"
)

func (interp *Interpreter) rlGui(env *Env) {
	nf := func(name string, fn func([]value.Value) (value.Value, error)) {
		env.Define(name, value.NewNativeFunc("rl."+name, fn))
	}

	// ── Style ──
	nf("gui_load_style", func(a []value.Value) (value.Value, error) { gui.LoadStyle(a[0].AsString()); return value.Void, nil })
	nf("gui_load_style_default", func(a []value.Value) (value.Value, error) { gui.LoadStyleDefault(); return value.Void, nil })
	nf("gui_enable", func(a []value.Value) (value.Value, error) { gui.Enable(); return value.Void, nil })
	nf("gui_disable", func(a []value.Value) (value.Value, error) { gui.Disable(); return value.Void, nil })
	nf("gui_lock", func(a []value.Value) (value.Value, error) { gui.Lock(); return value.Void, nil })
	nf("gui_unlock", func(a []value.Value) (value.Value, error) { gui.Unlock(); return value.Void, nil })
	nf("gui_is_locked", func(a []value.Value) (value.Value, error) { return value.NewBool(gui.IsLocked()), nil })
	nf("gui_set_alpha", func(a []value.Value) (value.Value, error) { gui.SetAlpha(f32(a[0])); return value.Void, nil })
	nf("gui_set_state", func(a []value.Value) (value.Value, error) {
		gui.SetState(gui.PropertyValue(a[0].AsI32()))
		return value.Void, nil
	})
	nf("gui_set_font", func(a []value.Value) (value.Value, error) {
		gui.SetFont(*argNative[rl.Font](a[0]))
		return value.Void, nil
	})
	nf("gui_set_style", func(a []value.Value) (value.Value, error) {
		gui.SetStyle(gui.ControlID(a[0].AsI32()), gui.PropertyID(a[1].AsI32()), gui.PropertyValue(a[2].AsI32()))
		return value.Void, nil
	})
	nf("gui_set_icon_scale", func(a []value.Value) (value.Value, error) { gui.SetIconScale(a[0].AsI32()); return value.Void, nil })
	nf("gui_set_tooltip", func(a []value.Value) (value.Value, error) { gui.SetTooltip(a[0].AsString()); return value.Void, nil })
	nf("gui_enable_tooltip", func(a []value.Value) (value.Value, error) { gui.EnableTooltip(); return value.Void, nil })
	nf("gui_disable_tooltip", func(a []value.Value) (value.Value, error) { gui.DisableTooltip(); return value.Void, nil })

	// ── Container/separator ──
	nf("gui_window_box", func(a []value.Value) (value.Value, error) {
		return value.NewBool(gui.WindowBox(argRect(a[0]), a[1].AsString())), nil
	})
	nf("gui_group_box", func(a []value.Value) (value.Value, error) {
		gui.GroupBox(argRect(a[0]), a[1].AsString())
		return value.Void, nil
	})
	nf("gui_line", func(a []value.Value) (value.Value, error) {
		gui.Line(argRect(a[0]), a[1].AsString())
		return value.Void, nil
	})
	nf("gui_panel", func(a []value.Value) (value.Value, error) {
		gui.Panel(argRect(a[0]), a[1].AsString())
		return value.Void, nil
	})
	nf("gui_status_bar", func(a []value.Value) (value.Value, error) {
		gui.StatusBar(argRect(a[0]), a[1].AsString())
		return value.Void, nil
	})
	nf("gui_dummy_rec", func(a []value.Value) (value.Value, error) {
		gui.DummyRec(argRect(a[0]), a[1].AsString())
		return value.Void, nil
	})

	// ── Basic controls ──
	nf("gui_label", func(a []value.Value) (value.Value, error) {
		gui.Label(argRect(a[0]), a[1].AsString())
		return value.Void, nil
	})
	nf("gui_button", func(a []value.Value) (value.Value, error) {
		return value.NewBool(gui.Button(argRect(a[0]), a[1].AsString())), nil
	})
	nf("gui_label_button", func(a []value.Value) (value.Value, error) {
		return value.NewBool(gui.LabelButton(argRect(a[0]), a[1].AsString())), nil
	})
	nf("gui_toggle", func(a []value.Value) (value.Value, error) {
		return value.NewBool(gui.Toggle(argRect(a[0]), a[1].AsString(), a[2].AsBool())), nil
	})
	nf("gui_toggle_group", func(a []value.Value) (value.Value, error) {
		return value.NewI32(gui.ToggleGroup(argRect(a[0]), a[1].AsString(), a[2].AsI32())), nil
	})
	nf("gui_toggle_slider", func(a []value.Value) (value.Value, error) {
		return value.NewI32(gui.ToggleSlider(argRect(a[0]), a[1].AsString(), a[2].AsI32())), nil
	})
	nf("gui_check_box", func(a []value.Value) (value.Value, error) {
		return value.NewBool(gui.CheckBox(argRect(a[0]), a[1].AsString(), a[2].AsBool())), nil
	})
	nf("gui_combo_box", func(a []value.Value) (value.Value, error) {
		return value.NewI32(gui.ComboBox(argRect(a[0]), a[1].AsString(), a[2].AsI32())), nil
	})

	// ── Sliders ──
	nf("gui_slider", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(gui.Slider(argRect(a[0]), a[1].AsString(), a[2].AsString(), f32(a[3]), f32(a[4]), f32(a[5])))), nil
	})
	nf("gui_slider_bar", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(gui.SliderBar(argRect(a[0]), a[1].AsString(), a[2].AsString(), f32(a[3]), f32(a[4]), f32(a[5])))), nil
	})
	nf("gui_progress_bar", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(gui.ProgressBar(argRect(a[0]), a[1].AsString(), a[2].AsString(), f32(a[3]), f32(a[4]), f32(a[5])))), nil
	})
	nf("gui_scroll_bar", func(a []value.Value) (value.Value, error) {
		return value.NewI32(gui.ScrollBar(argRect(a[0]), a[1].AsI32(), a[2].AsI32(), a[3].AsI32())), nil
	})

	// ── Text input ──
	nf("gui_text_box", func(a []value.Value) (value.Value, error) {
		text := a[1].AsString()
		editMode := a[3].AsBool()
		pressed := gui.TextBox(argRect(a[0]), &text, int(a[2].AsI32()), editMode)
		if pressed {
			editMode = !editMode
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(text), value.NewBool(editMode)}}
	})
	nf("gui_spinner", func(a []value.Value) (value.Value, error) {
		v := a[2].AsI32()
		edited := gui.Spinner(argRect(a[0]), a[1].AsString(), &v, int(a[3].AsI32()), int(a[4].AsI32()), a[5].AsBool())
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(v), value.NewBool(edited)}}
	})
	nf("gui_value_box", func(a []value.Value) (value.Value, error) {
		v := a[2].AsI32()
		edited := gui.ValueBox(argRect(a[0]), a[1].AsString(), &v, int(a[3].AsI32()), int(a[4].AsI32()), a[5].AsBool())
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(v), value.NewBool(edited)}}
	})

	// ── Advanced controls ──
	nf("gui_message_box", func(a []value.Value) (value.Value, error) {
		return value.NewI32(gui.MessageBox(argRect(a[0]), a[1].AsString(), a[2].AsString(), a[3].AsString())), nil
	})
	nf("gui_text_input_box", func(a []value.Value) (value.Value, error) {
		text := a[4].AsString()
		btn := gui.TextInputBox(argRect(a[0]), a[1].AsString(), a[2].AsString(), a[3].AsString(), &text, a[5].AsI32(), nil)
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(btn), value.NewString(text)}}
	})
	nf("gui_color_picker", func(a []value.Value) (value.Value, error) {
		c := gui.ColorPicker(argRect(a[0]), a[1].AsString(), argsColor(a[2:]))
		return valColor(c), nil
	})
	nf("gui_color_bar_alpha", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(gui.ColorBarAlpha(argRect(a[0]), a[1].AsString(), f32(a[2])))), nil
	})
	nf("gui_color_bar_hue", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(gui.ColorBarHue(argRect(a[0]), a[1].AsString(), f32(a[2])))), nil
	})
	nf("gui_dropdown_box", func(a []value.Value) (value.Value, error) {
		active := a[2].AsI32()
		pressed := gui.DropdownBox(argRect(a[0]), a[1].AsString(), &active, a[3].AsBool())
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(active), value.NewBool(pressed)}}
	})
	nf("gui_list_view", func(a []value.Value) (value.Value, error) {
		scrollIdx := a[2].AsI32()
		active := gui.ListView(argRect(a[0]), a[1].AsString(), &scrollIdx, a[3].AsI32())
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(active), value.NewI32(scrollIdx)}}
	})
	nf("gui_tab_bar", func(a []value.Value) (value.Value, error) {
		arr := a[1].AsArray()
		tabs := make([]string, len(arr.Elems))
		for i, e := range arr.Elems {
			tabs[i] = e.AsString()
		}
		active := a[2].AsI32()
		result := gui.TabBar(argRect(a[0]), tabs, &active)
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(result), value.NewI32(active)}}
	})

	// ── GUI control IDs ──
	env.Define("GUI_DEFAULT", value.NewI32(int32(gui.DEFAULT)))
	env.Define("GUI_LABEL", value.NewI32(int32(gui.LABEL)))
	env.Define("GUI_BUTTON", value.NewI32(int32(gui.BUTTON)))
	env.Define("GUI_TOGGLE", value.NewI32(int32(gui.TOGGLE)))
	env.Define("GUI_SLIDER", value.NewI32(int32(gui.SLIDER)))
	env.Define("GUI_PROGRESSBAR", value.NewI32(int32(gui.PROGRESSBAR)))
	env.Define("GUI_CHECKBOX", value.NewI32(int32(gui.CHECKBOX)))
	env.Define("GUI_COMBOBOX", value.NewI32(int32(gui.COMBOBOX)))
	env.Define("GUI_DROPDOWNBOX", value.NewI32(int32(gui.DROPDOWNBOX)))
	env.Define("GUI_TEXTBOX", value.NewI32(int32(gui.TEXTBOX)))
	env.Define("GUI_VALUEBOX", value.NewI32(int32(gui.VALUEBOX)))
	env.Define("GUI_LISTVIEW", value.NewI32(int32(gui.LISTVIEW)))
	env.Define("GUI_COLORPICKER", value.NewI32(int32(gui.COLORPICKER)))
	env.Define("GUI_SCROLLBAR", value.NewI32(int32(gui.SCROLLBAR)))
	env.Define("GUI_STATUSBAR", value.NewI32(int32(gui.STATUSBAR)))

	// ── GUI property IDs ──
	env.Define("GUI_TEXT_SIZE", value.NewI32(int32(gui.TEXT_SIZE)))
	env.Define("GUI_TEXT_SPACING", value.NewI32(int32(gui.TEXT_SPACING)))
	env.Define("GUI_BORDER_WIDTH", value.NewI32(int32(gui.BORDER_WIDTH)))
	env.Define("GUI_BORDER_COLOR_NORMAL", value.NewI32(int32(gui.BORDER_COLOR_NORMAL)))
	env.Define("GUI_BASE_COLOR_NORMAL", value.NewI32(int32(gui.BASE_COLOR_NORMAL)))
	env.Define("GUI_TEXT_COLOR_NORMAL", value.NewI32(int32(gui.TEXT_COLOR_NORMAL)))
	env.Define("GUI_BORDER_COLOR_FOCUSED", value.NewI32(int32(gui.BORDER_COLOR_FOCUSED)))
	env.Define("GUI_BASE_COLOR_FOCUSED", value.NewI32(int32(gui.BASE_COLOR_FOCUSED)))
	env.Define("GUI_TEXT_COLOR_FOCUSED", value.NewI32(int32(gui.TEXT_COLOR_FOCUSED)))
	env.Define("GUI_BORDER_COLOR_PRESSED", value.NewI32(int32(gui.BORDER_COLOR_PRESSED)))
	env.Define("GUI_BASE_COLOR_PRESSED", value.NewI32(int32(gui.BASE_COLOR_PRESSED)))
	env.Define("GUI_TEXT_COLOR_PRESSED", value.NewI32(int32(gui.TEXT_COLOR_PRESSED)))

	// ── GUI states ──
	env.Define("GUI_STATE_NORMAL", value.NewI32(int32(gui.STATE_NORMAL)))
	env.Define("GUI_STATE_FOCUSED", value.NewI32(int32(gui.STATE_FOCUSED)))
	env.Define("GUI_STATE_PRESSED", value.NewI32(int32(gui.STATE_PRESSED)))
	env.Define("GUI_STATE_DISABLED", value.NewI32(int32(gui.STATE_DISABLED)))
}
