# gui Tk Parity Roadmap

This roadmap tracks BASL `gui` progress toward a Tk-complete developer experience while keeping a strict native-widget rule.

## Non-Negotiables

- Native OS widgets only.
- No custom paint/theming layer.
- Honor system appearance automatically (including dark mode).
- Consistent BASL API style: typed opts objects + explicit `err` returns.

## Current Coverage

Implemented:
- App/Window lifecycle
- Layout: `Grid`, `Box`
- Core controls: `Label`, `Button`, `Entry`, `Checkbox`, `Select`, `TextArea`, `Progress`
- Forms/common controls: `Frame`, `Group`, `Radio`, `Scale`, `Spinbox`, `Separator`
- Data/navigation controls: `Tabs`, `Paned`, `List`, `Tree`

## Parity Phases

### Phase 1 (completed in this cycle)

- `Checkbox`
- `Select` (dropdown)
- `TextArea`
- `Progress`

### Phase 2 (forms and common controls, completed)

- `Radio` / radio groups
- `Scale` (slider)
- `Spinbox`
- `Separator`
- `Frame` / `LabelFrame` equivalent

### Phase 3 (data and navigation, completed)

- `List` / listbox
- `Tree` / treeview
- `Tabs` / notebook
- `Paned` split view

### Phase 4 (menus, dialogs, drawing)

- `Menu` + menu bar helpers
- File/open/save dialogs
- Message dialogs
- `Canvas` (native drawing surface)

## Platform Strategy

- macOS: Cocoa backend (current reference implementation)
- Linux: GTK backend using native GTK widgets
- Windows: Win32/WinUI backend using native controls

Backends must implement the same BASL API contract and keep native appearance behavior.
