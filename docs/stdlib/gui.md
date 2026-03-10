# gui

Cross-platform GUI API with native widgets.

Current backend support:
- macOS (`cocoa`) with cgo enabled
- other platforms currently return explicit `err.state` for constructor/runtime calls

Roadmap: see [gui-roadmap.md](./gui-roadmap.md) for Tk parity phases.
API stability contract for this release: [gui-api-freeze-v0.1.3.md](./gui-api-freeze-v0.1.3.md).

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
### gui.frame_opts() -> gui.FrameOpts
### gui.group_opts(string title) -> gui.GroupOpts
### gui.radio_opts() -> gui.RadioOpts
### gui.scale_opts() -> gui.ScaleOpts
### gui.spinbox_opts() -> gui.SpinboxOpts
### gui.separator_opts() -> gui.SeparatorOpts
### gui.tabs_opts() -> gui.TabsOpts
### gui.paned_opts() -> gui.PanedOpts
### gui.list_opts() -> gui.ListOpts
### gui.tree_opts() -> gui.TreeOpts
### gui.menu_bar_opts() -> gui.MenuBarOpts
### gui.menu_opts(string title) -> gui.MenuOpts
### gui.canvas_opts() -> gui.CanvasOpts
### gui.file_dialog_opts(string title) -> gui.FileDialogOpts
### gui.message_opts(string title, string message) -> gui.MessageOpts

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
### gui.frame(gui.FrameOpts opts) -> (gui.Frame, err)
### gui.group(gui.GroupOpts opts) -> (gui.Group, err)
### gui.radio(gui.RadioOpts opts) -> (gui.Radio, err)
### gui.scale(gui.ScaleOpts opts) -> (gui.Scale, err)
### gui.spinbox(gui.SpinboxOpts opts) -> (gui.Spinbox, err)
### gui.separator(gui.SeparatorOpts opts) -> (gui.Separator, err)
### gui.tabs(gui.TabsOpts opts) -> (gui.Tabs, err)
### gui.paned(gui.PanedOpts opts) -> (gui.Paned, err)
### gui.list(gui.ListOpts opts) -> (gui.List, err)
### gui.tree(gui.TreeOpts opts) -> (gui.Tree, err)
### gui.menu_bar(gui.MenuBarOpts opts) -> (gui.MenuBar, err)
### gui.menu(gui.MenuOpts opts) -> (gui.Menu, err)
### gui.canvas(gui.CanvasOpts opts) -> (gui.Canvas, err)

## Dialog APIs

### gui.open_file(gui.FileDialogOpts opts) -> (string path, err)
### gui.save_file(gui.FileDialogOpts opts) -> (string path, err)
### gui.open_directory(gui.FileDialogOpts opts) -> (string path, err)
### gui.info(gui.MessageOpts opts) -> err
### gui.warn(gui.MessageOpts opts) -> err
### gui.error(gui.MessageOpts opts) -> err
### gui.confirm(gui.MessageOpts opts) -> (bool confirmed, err)

Dialog cancel behavior:
- `open_file`, `save_file`, and `open_directory` return `""` path with `err == ok` when cancelled.

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

### gui.FrameOpts

| Field | Type | Default |
|------|------|---------|
| `padding` | `i32` | `8` |

### gui.GroupOpts

| Field | Type | Default |
|------|------|---------|
| `title` | `string` | required via `group_opts(title)` |
| `padding` | `i32` | `10` |

### gui.RadioOpts

| Field | Type | Default |
|------|------|---------|
| `options` | `array<string>` | `[]` |
| `selected` | `i32` | `0` |
| `vertical` | `bool` | `true` |
| `on_change` | `fn` | unset |

### gui.ScaleOpts

| Field | Type | Default |
|------|------|---------|
| `min` | `f64` | `0.0` |
| `max` | `f64` | `100.0` |
| `value` | `f64` | `0.0` |
| `vertical` | `bool` | `false` |
| `width` | `i32` | `220` |
| `on_change` | `fn` | unset |

### gui.SpinboxOpts

| Field | Type | Default |
|------|------|---------|
| `min` | `f64` | `0.0` |
| `max` | `f64` | `100.0` |
| `step` | `f64` | `1.0` |
| `value` | `f64` | `0.0` |
| `width` | `i32` | `120` |
| `on_change` | `fn` | unset |

### gui.SeparatorOpts

| Field | Type | Default |
|------|------|---------|
| `vertical` | `bool` | `false` |
| `length` | `i32` | `160` |

### gui.TabsOpts

| Field | Type | Default |
|------|------|---------|
| `selected` | `i32` | `0` |
| `on_change` | `fn` | unset |

### gui.PanedOpts

| Field | Type | Default |
|------|------|---------|
| `vertical` | `bool` | `false` |
| `ratio` | `f64` | `0.5` |
| `on_change` | `fn` | unset |

`vertical = true` means top/bottom split. `vertical = false` means left/right split.

### gui.ListOpts

| Field | Type | Default |
|------|------|---------|
| `items` | `array<string>` | `[]` |
| `selected` | `i32` | `0` |
| `width` | `i32` | `280` |
| `height` | `i32` | `180` |
| `on_change` | `fn` | unset |

### gui.TreeOpts

| Field | Type | Default |
|------|------|---------|
| `width` | `i32` | `320` |
| `height` | `i32` | `220` |
| `on_change` | `fn` | unset |

### gui.MenuBarOpts

No fields (reserved for future menu bar configuration).

### gui.MenuOpts

| Field | Type | Default |
|------|------|---------|
| `title` | `string` | required via `menu_opts(title)` |

### gui.CanvasOpts

| Field | Type | Default |
|------|------|---------|
| `width` | `i32` | `420` |
| `height` | `i32` | `260` |

### gui.FileDialogOpts

| Field | Type | Default |
|------|------|---------|
| `title` | `string` | required via `file_dialog_opts(title)` |
| `directory` | `string` | `""` |
| `file_name` | `string` | `""` |
| `extensions` | `array<string>` | `[]` |

### gui.MessageOpts

| Field | Type | Default |
|------|------|---------|
| `title` | `string` | required via `message_opts(title, message)` |
| `message` | `string` | required via `message_opts(title, message)` |

## gui.App Methods

### app.window(gui.WindowOpts opts) -> (gui.Window, err)
### app.run() -> err
### app.quit() -> err
### app.set_menu_bar(gui.MenuBar menu_bar) -> err

## gui.Window Methods

### win.set_child(widget) -> err
Supports `gui.Box`, `gui.Grid`, `gui.Frame`, `gui.Group`, `gui.Tabs`, `gui.Paned`, and all control widgets.

### win.set_title(string title) -> err
### win.show() -> err
### win.close() -> err

## Container Methods

### box.add(widget) -> err
### grid.place(widget, gui.CellOpts cell) -> err
### frame.set_child(widget) -> err
### group.set_child(widget) -> err
### group.set_title(string title) -> err
### tabs.add_tab(string title, widget) -> err
### paned.set_first(widget) -> err
### paned.set_second(widget) -> err
### menu_bar.add_menu(gui.Menu menu) -> err
### menu.add_item(string title, fn callback) -> err
### menu.add_separator() -> err
### menu.add_submenu(gui.Menu menu) -> err

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

### radio.selected_index() -> (i32, err)
### radio.set_selected_index(i32 index) -> err
### radio.selected_text() -> (string, err)
### radio.on_change(fn callback) -> err

### scale.value() -> (f64, err)
### scale.set_value(f64 value) -> err
### scale.on_change(fn callback) -> err

### spinbox.value() -> (f64, err)
### spinbox.set_value(f64 value) -> err
### spinbox.on_change(fn callback) -> err

`gui.Separator` is structural and currently has no instance methods.

### tabs.selected_index() -> (i32, err)
### tabs.set_selected_index(i32 index) -> err
### tabs.on_change(fn callback) -> err

### paned.ratio() -> (f64, err)
### paned.set_ratio(f64 ratio) -> err
### paned.on_change(fn callback) -> err

### list.selected_index() -> (i32, err)
### list.set_selected_index(i32 index) -> err
### list.selected_text() -> (string, err)
### list.add_item(string text) -> err
### list.clear() -> err
### list.on_change(fn callback) -> err

### tree.add_root(string title) -> (i32 node_id, err)
### tree.add_child(i32 parent_id, string title) -> (i32 node_id, err)
### tree.set_text(i32 node_id, string title) -> err
### tree.selected_id() -> (i32 node_id, err)
### tree.set_selected_id(i32 node_id) -> err
### tree.selected_text() -> (string, err)
### tree.clear() -> err
### tree.on_change(fn callback) -> err

Tree nodes are addressed by explicit `node_id` values returned from `add_root` / `add_child`.

### canvas.clear() -> err
### canvas.set_color(f64 r, f64 g, f64 b, f64 a) -> err
### canvas.line(f64 x1, f64 y1, f64 x2, f64 y2, f64 width) -> err
### canvas.rect(f64 x, f64 y, f64 w, f64 h, bool fill, f64 line_width, f64 corner_radius) -> err
### canvas.circle(f64 x, f64 y, f64 radius, bool fill, f64 line_width) -> err
### canvas.text(f64 x, f64 y, string text, f64 size) -> err

Canvas command safety:
- Drawing commands accumulate until `canvas.clear()`.
- For long-running redraw loops, call `canvas.clear()` periodically.
