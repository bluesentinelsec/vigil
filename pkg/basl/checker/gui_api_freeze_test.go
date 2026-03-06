package checker

import (
	"fmt"
	"sort"
	"strings"
	"testing"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
)

func TestBuiltinGuiAPIFreezeV013(t *testing.T) {
	mod := newBuiltinModule("gui")
	got := moduleFreezeSignature(mod)

	const want = `class gui.App
  method quit() -> err
  method run() -> err
  method set_menu_bar(gui.MenuBar) -> err
  method window(gui.WindowOpts) -> (gui.Window, err)
class gui.AppOpts
class gui.Box
  method add(any) -> err
class gui.BoxOpts
  field padding: i32
  field spacing: i32
  field vertical: bool
class gui.Button
  method on_click(fn) -> err
  method set_text(string) -> err
class gui.ButtonOpts
  field height: i32
  field on_click: fn
  field text: string
  field width: i32
class gui.Canvas
  method circle(f64, f64, f64, bool, f64) -> err
  method clear() -> err
  method line(f64, f64, f64, f64, f64) -> err
  method rect(f64, f64, f64, f64, bool, f64, f64) -> err
  method set_color(f64, f64, f64, f64) -> err
  method text(f64, f64, string, f64) -> err
class gui.CanvasOpts
  field height: i32
  field width: i32
class gui.CellOpts
  field col: i32
  field col_span: i32
  field fill_x: bool
  field fill_y: bool
  field row: i32
  field row_span: i32
class gui.Checkbox
  method checked() -> (bool, err)
  method on_toggle(fn) -> err
  method set_checked(bool) -> err
  method set_text(string) -> err
class gui.CheckboxOpts
  field checked: bool
  field on_toggle: fn
  field text: string
class gui.Entry
  method set_text(string) -> err
  method text() -> (string, err)
class gui.EntryOpts
  field text: string
  field width: i32
class gui.FileDialogOpts
  field directory: string
  field extensions: array<string>
  field file_name: string
  field title: string
class gui.Frame
  method set_child(any) -> err
class gui.FrameOpts
  field padding: i32
class gui.Grid
  method place(any, gui.CellOpts) -> err
class gui.GridOpts
  field col_spacing: i32
  field row_spacing: i32
class gui.Group
  method set_child(any) -> err
  method set_title(string) -> err
class gui.GroupOpts
  field padding: i32
  field title: string
class gui.Label
  method set_text(string) -> err
class gui.LabelOpts
  field text: string
class gui.List
  method add_item(string) -> err
  method clear() -> err
  method on_change(fn) -> err
  method selected_index() -> (i32, err)
  method selected_text() -> (string, err)
  method set_selected_index(i32) -> err
class gui.ListOpts
  field height: i32
  field items: array<string>
  field on_change: fn
  field selected: i32
  field width: i32
class gui.Menu
  method add_item(string, fn) -> err
  method add_separator() -> err
  method add_submenu(gui.Menu) -> err
class gui.MenuBar
  method add_menu(gui.Menu) -> err
class gui.MenuBarOpts
class gui.MenuOpts
  field title: string
class gui.MessageOpts
  field message: string
  field title: string
class gui.Paned
  method on_change(fn) -> err
  method ratio() -> (f64, err)
  method set_first(any) -> err
  method set_ratio(f64) -> err
  method set_second(any) -> err
class gui.PanedOpts
  field on_change: fn
  field ratio: f64
  field vertical: bool
class gui.Progress
  method set_value(f64) -> err
  method value() -> (f64, err)
class gui.ProgressOpts
  field indeterminate: bool
  field max: f64
  field min: f64
  field value: f64
  field width: i32
class gui.Radio
  method on_change(fn) -> err
  method selected_index() -> (i32, err)
  method selected_text() -> (string, err)
  method set_selected_index(i32) -> err
class gui.RadioOpts
  field on_change: fn
  field options: array<string>
  field selected: i32
  field vertical: bool
class gui.Scale
  method on_change(fn) -> err
  method set_value(f64) -> err
  method value() -> (f64, err)
class gui.ScaleOpts
  field max: f64
  field min: f64
  field on_change: fn
  field value: f64
  field vertical: bool
  field width: i32
class gui.Select
  method add_item(string) -> err
  method on_change(fn) -> err
  method selected_index() -> (i32, err)
  method selected_text() -> (string, err)
  method set_selected_index(i32) -> err
class gui.SelectOpts
  field on_change: fn
  field options: array<string>
  field selected: i32
  field width: i32
class gui.Separator
class gui.SeparatorOpts
  field length: i32
  field vertical: bool
class gui.Spinbox
  method on_change(fn) -> err
  method set_value(f64) -> err
  method value() -> (f64, err)
class gui.SpinboxOpts
  field max: f64
  field min: f64
  field on_change: fn
  field step: f64
  field value: f64
  field width: i32
class gui.Tabs
  method add_tab(string, any) -> err
  method on_change(fn) -> err
  method selected_index() -> (i32, err)
  method set_selected_index(i32) -> err
class gui.TabsOpts
  field on_change: fn
  field selected: i32
class gui.TextArea
  method append(string) -> err
  method set_text(string) -> err
  method text() -> (string, err)
class gui.TextAreaOpts
  field height: i32
  field text: string
  field width: i32
class gui.Tree
  method add_child(i32, string) -> (i32, err)
  method add_root(string) -> (i32, err)
  method clear() -> err
  method on_change(fn) -> err
  method selected_id() -> (i32, err)
  method selected_text() -> (string, err)
  method set_selected_id(i32) -> err
  method set_text(i32, string) -> err
class gui.TreeOpts
  field height: i32
  field on_change: fn
  field width: i32
class gui.Window
  method close() -> err
  method set_child(any) -> err
  method set_title(string) -> err
  method show() -> err
class gui.WindowOpts
  field height: i32
  field title: string
  field width: i32
fn app(gui.AppOpts) -> (gui.App, err)
fn app_opts() -> gui.AppOpts
fn backend() -> string
fn box(gui.BoxOpts) -> (gui.Box, err)
fn box_opts() -> gui.BoxOpts
fn button(gui.ButtonOpts) -> (gui.Button, err)
fn button_opts(string) -> gui.ButtonOpts
fn canvas(gui.CanvasOpts) -> (gui.Canvas, err)
fn canvas_opts() -> gui.CanvasOpts
fn cell_opts(i32, i32) -> gui.CellOpts
fn checkbox(gui.CheckboxOpts) -> (gui.Checkbox, err)
fn checkbox_opts(string) -> gui.CheckboxOpts
fn confirm(gui.MessageOpts) -> (bool, err)
fn entry(gui.EntryOpts) -> (gui.Entry, err)
fn entry_opts() -> gui.EntryOpts
fn error(gui.MessageOpts) -> err
fn file_dialog_opts(string) -> gui.FileDialogOpts
fn frame(gui.FrameOpts) -> (gui.Frame, err)
fn frame_opts() -> gui.FrameOpts
fn grid(gui.GridOpts) -> (gui.Grid, err)
fn grid_opts() -> gui.GridOpts
fn group(gui.GroupOpts) -> (gui.Group, err)
fn group_opts(string) -> gui.GroupOpts
fn hbox() -> (gui.Box, err)
fn info(gui.MessageOpts) -> err
fn label(gui.LabelOpts) -> (gui.Label, err)
fn label_opts(string) -> gui.LabelOpts
fn list(gui.ListOpts) -> (gui.List, err)
fn list_opts() -> gui.ListOpts
fn menu(gui.MenuOpts) -> (gui.Menu, err)
fn menu_bar(gui.MenuBarOpts) -> (gui.MenuBar, err)
fn menu_bar_opts() -> gui.MenuBarOpts
fn menu_opts(string) -> gui.MenuOpts
fn message_opts(string, string) -> gui.MessageOpts
fn open_directory(gui.FileDialogOpts) -> (string, err)
fn open_file(gui.FileDialogOpts) -> (string, err)
fn paned(gui.PanedOpts) -> (gui.Paned, err)
fn paned_opts() -> gui.PanedOpts
fn progress(gui.ProgressOpts) -> (gui.Progress, err)
fn progress_opts() -> gui.ProgressOpts
fn radio(gui.RadioOpts) -> (gui.Radio, err)
fn radio_opts() -> gui.RadioOpts
fn save_file(gui.FileDialogOpts) -> (string, err)
fn scale(gui.ScaleOpts) -> (gui.Scale, err)
fn scale_opts() -> gui.ScaleOpts
fn select(gui.SelectOpts) -> (gui.Select, err)
fn select_opts() -> gui.SelectOpts
fn separator(gui.SeparatorOpts) -> (gui.Separator, err)
fn separator_opts() -> gui.SeparatorOpts
fn spinbox(gui.SpinboxOpts) -> (gui.Spinbox, err)
fn spinbox_opts() -> gui.SpinboxOpts
fn supported() -> bool
fn tabs(gui.TabsOpts) -> (gui.Tabs, err)
fn tabs_opts() -> gui.TabsOpts
fn textarea(gui.TextAreaOpts) -> (gui.TextArea, err)
fn textarea_opts() -> gui.TextAreaOpts
fn tree(gui.TreeOpts) -> (gui.Tree, err)
fn tree_opts() -> gui.TreeOpts
fn vbox() -> (gui.Box, err)
fn warn(gui.MessageOpts) -> err
fn window_opts(string) -> gui.WindowOpts`
	if got != want {
		t.Fatalf("gui API freeze mismatch (-want +got):\n%s", got)
	}
}

func moduleFreezeSignature(mod *moduleInfo) string {
	var b strings.Builder

	exportNames := sortedKeys(mod.exports)
	for _, name := range exportNames {
		sym := mod.exports[name]
		switch sym.kind {
		case symbolFn:
			fmt.Fprintf(&b, "fn %s%s\n", sym.name, freezeFuncSig(sym.fn))
		case symbolConst:
			fmt.Fprintf(&b, "const %s: %s\n", sym.name, freezeTypeString(sym.typ))
		case symbolClass:
			fmt.Fprintf(&b, "class %s\n", sym.class.name)
			for _, field := range sortedTypeKeys(sym.class.fields) {
				fmt.Fprintf(&b, "  field %s: %s\n", field, freezeTypeString(sym.class.fields[field]))
			}
			for _, method := range sortedFuncKeys(sym.class.methods) {
				fmt.Fprintf(&b, "  method %s%s\n", method, freezeFuncSig(sym.class.methods[method]))
			}
		}
	}

	return strings.TrimRight(b.String(), "\n")
}

func freezeFuncSig(sig *funcSig) string {
	if sig == nil {
		return "() -> void"
	}

	parts := make([]string, 0, len(sig.params))
	for _, p := range sig.params {
		parts = append(parts, freezeTypeString(p))
	}
	params := strings.Join(parts, ", ")
	ret := freezeReturns(sig.ret)

	// Preserve optional/variadic shape in freeze output if ever used.
	if sig.hasArityBounds {
		return fmt.Sprintf("(%s) [%d..%s] -> %s", params, sig.minArgs, freezeMax(sig.maxArgs), ret)
	}
	if sig.variadic {
		return fmt.Sprintf("(%s...) -> %s", params, ret)
	}
	return fmt.Sprintf("(%s) -> %s", params, ret)
}

func freezeReturns(rets []*ast.TypeExpr) string {
	if len(rets) == 0 {
		return "void"
	}
	if len(rets) == 1 {
		return freezeTypeString(rets[0])
	}
	parts := make([]string, 0, len(rets))
	for _, r := range rets {
		parts = append(parts, freezeTypeString(r))
	}
	return "(" + strings.Join(parts, ", ") + ")"
}

func freezeMax(max int) string {
	if max < 0 {
		return "inf"
	}
	return fmt.Sprintf("%d", max)
}

func freezeTypeString(t *ast.TypeExpr) string {
	if t == nil {
		return "any"
	}
	return typeString(t)
}

func sortedKeys[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func sortedTypeKeys(m map[string]*ast.TypeExpr) []string {
	return sortedKeys(m)
}

func sortedFuncKeys(m map[string]*funcSig) []string {
	return sortedKeys(m)
}
