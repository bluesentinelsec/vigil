# gui

Cross-platform GUI API with native widgets.

Current backend support:
- macOS (`cocoa`) with cgo enabled
- other platforms currently return explicit `err.state` for constructor/runtime calls

```
import "gui";
```

## Design

The API is intentionally TK-like and small:
- `gui.app()` creates the application object
- `app.window(...)` creates top-level windows
- `gui.vbox()` / `gui.hbox()` layout widgets
- `gui.label(...)`, `gui.button(...)`, `gui.entry()` create controls

## Functions

### gui.supported() -> bool

Returns whether the current runtime supports native GUI operations.

### gui.backend() -> string

Returns backend name:
- `"cocoa"` on macOS backend
- `"unsupported"` where GUI is not available

### gui.app() -> (gui.App, err)

Creates the GUI app context.

### gui.vbox() -> (gui.Box, err)

Creates a vertical layout container.

### gui.hbox() -> (gui.Box, err)

Creates a horizontal layout container.

### gui.label(string text) -> (gui.Label, err)

Creates a label widget.

### gui.button(string text) -> (gui.Button, err)

Creates a button widget.

### gui.entry() -> (gui.Entry, err)

Creates a single-line text input widget.

## gui.App Methods

### app.window(string title, i32 width, i32 height) -> (gui.Window, err)

Creates a top-level window.

### app.run() -> err

Runs the GUI event loop.

### app.quit() -> err

Stops the GUI event loop.

## gui.Window Methods

### win.set_child(widget) -> err

Sets the window content widget (`gui.Box`, `gui.Label`, `gui.Button`, or `gui.Entry`).

### win.set_title(string title) -> err

Updates window title.

### win.show() -> err

Shows the window.

### win.close() -> err

Closes the window.

## gui.Box Methods

### box.add(widget) -> err

Adds a child widget (`gui.Box`, `gui.Label`, `gui.Button`, or `gui.Entry`).

## gui.Label Methods

### label.set_text(string text) -> err

Updates label text.

## gui.Button Methods

### button.set_text(string text) -> err

Updates button text.

### button.on_click(fn callback) -> err

Registers a zero-argument callback invoked when the button is clicked.

## gui.Entry Methods

### entry.text() -> (string, err)

Reads current text value.

### entry.set_text(string text) -> err

Updates text value.

## Example

```c
import "fmt";
import "gui";

fn main() -> i32 {
    if (!gui.supported()) {
        fmt.eprintln(f"gui backend unavailable: {gui.backend()}");
        return 1;
    }

    gui.App app, err appErr = gui.app();
    guard gui.Window win, err winErr = app.window("BASL GUI", 480, 320) {
        fmt.eprintln(f"window: {winErr.message()}");
        return 1;
    }
    guard gui.Box root, err boxErr = gui.vbox() {
        fmt.eprintln(f"box: {boxErr.message()}");
        return 1;
    }
    guard gui.Label lbl, err lblErr = gui.label("Hello from BASL") {
        fmt.eprintln(f"label: {lblErr.message()}");
        return 1;
    }
    guard gui.Button quitBtn, err btnErr = gui.button("Quit") {
        fmt.eprintln(f"button: {btnErr.message()}");
        return 1;
    }
    quitBtn.on_click(fn() -> void {
        fmt.println("clicked");
    });

    root.add(lbl);
    root.add(quitBtn);
    win.set_child(root);
    win.show();
    app.run();
    return 0;
}
```
