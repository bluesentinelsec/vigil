# gui

Cross-platform GUI API with native widgets.

Current backend support:
- macOS (`cocoa`) with cgo enabled
- other platforms currently return explicit `err.state` for constructor/runtime calls

```
import "gui";
```

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

## Widget Constructors

### gui.app(gui.AppOpts opts) -> (gui.App, err)

Creates the GUI app context.

### gui.box(gui.BoxOpts opts) -> (gui.Box, err)

Creates a layout container.

### gui.grid(gui.GridOpts opts) -> (gui.Grid, err)

Creates a grid layout container. Use this as the primary layout API.

### gui.vbox() -> (gui.Box, err)
### gui.hbox() -> (gui.Box, err)

Convenience constructors for default vertical/horizontal boxes.

### gui.label(gui.LabelOpts opts) -> (gui.Label, err)
### gui.button(gui.ButtonOpts opts) -> (gui.Button, err)
### gui.entry(gui.EntryOpts opts) -> (gui.Entry, err)

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

## gui.App Methods

### app.window(gui.WindowOpts opts) -> (gui.Window, err)

Creates a top-level window.

### app.run() -> err

Runs the GUI event loop.

### app.quit() -> err

Stops the GUI event loop.

## gui.Window Methods

### win.set_child(widget) -> err

Sets the window content widget (`gui.Box`, `gui.Grid`, `gui.Label`, `gui.Button`, or `gui.Entry`).

### win.set_title(string title) -> err
### win.show() -> err
### win.close() -> err

## gui.Box Methods

### box.add(widget) -> err

Adds a child widget (`gui.Box`, `gui.Label`, `gui.Button`, or `gui.Entry`).

## gui.Grid Methods

### grid.place(widget, gui.CellOpts cell) -> err

Places a widget at `cell.row` and `cell.col`. Use `row_span` and `col_span` to span multiple cells.

## gui.Label Methods

### label.set_text(string text) -> err

## gui.Button Methods

### button.set_text(string text) -> err
### button.on_click(fn callback) -> err

Registers a zero-argument callback invoked when the button is clicked.

## gui.Entry Methods

### entry.text() -> (string, err)
### entry.set_text(string text) -> err

## Example

```c
import "fmt";
import "gui";

fn main() -> i32 {
    if (!gui.supported()) {
        fmt.eprintln(f"gui backend unavailable: {gui.backend()}");
        return 1;
    }

    gui.AppOpts appOpts = gui.app_opts();
    guard gui.App app, err appErr = gui.app(appOpts) {
        fmt.eprintln(f"gui.app: {appErr.message()}");
        return 1;
    }

    gui.WindowOpts w = gui.window_opts("BASL GUI");
    w.width = 480;
    w.height = 320;
    guard gui.Window win, err winErr = app.window(w) {
        fmt.eprintln(f"window: {winErr.message()}");
        return 1;
    }

    gui.GridOpts grid = gui.grid_opts();
    grid.row_spacing = 10;
    grid.col_spacing = 10;
    guard gui.Grid root, err gridErr = gui.grid(grid) {
        fmt.eprintln(f"grid: {gridErr.message()}");
        return 1;
    }

    gui.LabelOpts lo = gui.label_opts("Hello from BASL");
    guard gui.Label lbl, err lblErr = gui.label(lo) {
        fmt.eprintln(f"label: {lblErr.message()}");
        return 1;
    }

    gui.ButtonOpts bo = gui.button_opts("Click");
    bo.width = 120;
    bo.height = 32;
    bo.on_click = fn() -> void {
        fmt.println("clicked");
    };
    guard gui.Button btn, err btnErr = gui.button(bo) {
        fmt.eprintln(f"button: {btnErr.message()}");
        return 1;
    }

    gui.CellOpts header = gui.cell_opts(0, 0);
    header.col_span = 2;
    err placeHeaderErr = root.place(lbl, header);
    if (placeHeaderErr != ok) {
        fmt.eprintln(f"place header: {placeHeaderErr.message()}");
        return 1;
    }

    gui.CellOpts buttonCell = gui.cell_opts(1, 1);
    err placeButtonErr = root.place(btn, buttonCell);
    if (placeButtonErr != ok) {
        fmt.eprintln(f"place button: {placeButtonErr.message()}");
        return 1;
    }

    win.set_child(root);
    win.show();
    app.run();
    return 0;
}
```
