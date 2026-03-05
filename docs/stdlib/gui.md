# gui

Cross-platform GUI API with native widgets.

Current backend support:
- macOS (`cocoa`) with cgo enabled
- other platforms currently return explicit `err.state` for constructor/runtime calls

Roadmap: see [gui-roadmap.md](./gui-roadmap.md) for Tk parity phases.

```c
import "gui";
```

## Native Look And Theme

`gui` is native-first:
- BASL uses OS-native widgets directly (`NSButton`, `NSTextField`, `NSPopUpButton`, etc. on macOS).
- Apps follow system appearance automatically, including dark mode.
- BASL does not paint a custom cross-platform skin.

## Design

`gui` uses typed options objects to avoid long positional argument lists.

Recommended pattern:
1. Create opts with `*_opts(...)`
2. Set only fields you care about
3. Call constructor with opts

## Backend Info

### gui.supported() -> bool
Returns whether native GUI operations are available on this runtime.

### gui.backend() -> string
Returns backend name:
- `"cocoa"` on macOS backend
- `"unsupported"` where GUI is not available

## Options Constructors

### gui.app_opts() -> gui.AppOpts
### gui.window_opts(string title) -> gui.WindowOpts
### gui.box_opts() -> gui.BoxOpts
### gui.grid_opts() -> gui.GridOpts
### gui.cell_opts(i32 row, i32 col) -> gui.CellOpts
### gui.label_opts(string text) -> gui.LabelOpts
### gui.button_opts(string text) -> gui.ButtonOpts
### gui.entry_opts() -> gui.EntryOpts
### gui.checkbox_opts(string text) -> gui.CheckboxOpts
### gui.select_opts() -> gui.SelectOpts
### gui.textarea_opts() -> gui.TextAreaOpts
### gui.progress_opts() -> gui.ProgressOpts

## Widget Constructors

### gui.app(gui.AppOpts opts) -> (gui.App, err)
### gui.box(gui.BoxOpts opts) -> (gui.Box, err)
### gui.grid(gui.GridOpts opts) -> (gui.Grid, err)
### gui.vbox() -> (gui.Box, err)
### gui.hbox() -> (gui.Box, err)
### gui.label(gui.LabelOpts opts) -> (gui.Label, err)
### gui.button(gui.ButtonOpts opts) -> (gui.Button, err)
### gui.entry(gui.EntryOpts opts) -> (gui.Entry, err)
### gui.checkbox(gui.CheckboxOpts opts) -> (gui.Checkbox, err)
### gui.select(gui.SelectOpts opts) -> (gui.Select, err)
### gui.textarea(gui.TextAreaOpts opts) -> (gui.TextArea, err)
### gui.progress(gui.ProgressOpts opts) -> (gui.Progress, err)

## Option Types

### gui.AppOpts
Reserved for app-level configuration.

### gui.WindowOpts

| Field | Type | Default |
|------|------|---------|
| `title` | `string` | required via `window_opts(title)` |
| `width` | `i32` | `800` |
| `height` | `i32` | `600` |

### gui.BoxOpts

| Field | Type | Default |
|------|------|---------|
| `vertical` | `bool` | `true` |
| `spacing` | `i32` | `8` |
| `padding` | `i32` | `12` |

### gui.GridOpts

| Field | Type | Default |
|------|------|---------|
| `row_spacing` | `i32` | `8` |
| `col_spacing` | `i32` | `8` |

### gui.CellOpts

| Field | Type | Default |
|------|------|---------|
| `row` | `i32` | required via `cell_opts(row, col)` |
| `col` | `i32` | required via `cell_opts(row, col)` |
| `row_span` | `i32` | `1` |
| `col_span` | `i32` | `1` |
| `fill_x` | `bool` | `true` |
| `fill_y` | `bool` | `false` |

### gui.LabelOpts

| Field | Type | Default |
|------|------|---------|
| `text` | `string` | required via `label_opts(text)` |

### gui.ButtonOpts

| Field | Type | Default |
|------|------|---------|
| `text` | `string` | required via `button_opts(text)` |
| `width` | `i32` | `0` (auto) |
| `height` | `i32` | `0` (auto) |
| `on_click` | `fn` | unset |

### gui.EntryOpts

| Field | Type | Default |
|------|------|---------|
| `text` | `string` | `""` |
| `width` | `i32` | `240` |

### gui.CheckboxOpts

| Field | Type | Default |
|------|------|---------|
| `text` | `string` | required via `checkbox_opts(text)` |
| `checked` | `bool` | `false` |
| `on_toggle` | `fn` | unset |

### gui.SelectOpts

| Field | Type | Default |
|------|------|---------|
| `options` | `array<string>` | `[]` |
| `selected` | `i32` | `0` |
| `width` | `i32` | `240` |
| `on_change` | `fn` | unset |

### gui.TextAreaOpts

| Field | Type | Default |
|------|------|---------|
| `text` | `string` | `""` |
| `width` | `i32` | `420` |
| `height` | `i32` | `160` |

### gui.ProgressOpts

| Field | Type | Default |
|------|------|---------|
| `min` | `f64` | `0.0` |
| `max` | `f64` | `100.0` |
| `value` | `f64` | `0.0` |
| `indeterminate` | `bool` | `false` |
| `width` | `i32` | `200` |

## gui.App Methods

### app.window(gui.WindowOpts opts) -> (gui.Window, err)
### app.run() -> err
### app.quit() -> err

## gui.Window Methods

### win.set_child(widget) -> err
Supports `gui.Box`, `gui.Grid`, and all control widgets.

### win.set_title(string title) -> err
### win.show() -> err
### win.close() -> err

## gui.Box Methods

### box.add(widget) -> err

## gui.Grid Methods

### grid.place(widget, gui.CellOpts cell) -> err

`fill_x` / `fill_y` control whether a widget expands with window resize.

Responsive recommendation:
- Use `Grid` as the main layout container.
- Keep widget `width` as `0` unless you need fixed sizing.
- Use `cell.fill_x = true` for controls that should stretch horizontally.
- Use `cell.fill_y = true` for multiline areas.

## Control Methods

### label.set_text(string text) -> err

### button.set_text(string text) -> err
### button.on_click(fn callback) -> err

### entry.text() -> (string, err)
### entry.set_text(string text) -> err

### checkbox.checked() -> (bool, err)
### checkbox.set_checked(bool value) -> err
### checkbox.set_text(string text) -> err
### checkbox.on_toggle(fn callback) -> err

### select.selected_index() -> (i32, err)
### select.set_selected_index(i32 index) -> err
### select.selected_text() -> (string, err)
### select.add_item(string text) -> err
### select.on_change(fn callback) -> err

### textarea.text() -> (string, err)
### textarea.set_text(string text) -> err
### textarea.append(string text) -> err

### progress.value() -> (f64, err)
### progress.set_value(f64 value) -> err
