#include <Cocoa/Cocoa.h>
#include <float.h>
#include <objc/runtime.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void baslGuiInvokeCallback(uintptr_t callbackId);

@interface BaslControlTarget : NSObject
@property(nonatomic, assign) uintptr_t callbackId;
- (void)onAction:(id)sender;
@end

@implementation BaslControlTarget
- (void)onAction:(id)sender {
    baslGuiInvokeCallback(self.callbackId);
}
@end

@interface BaslRadioTarget : NSObject
@property(nonatomic, assign) uintptr_t callbackId;
@property(nonatomic, weak) NSStackView* group;
- (void)onSelect:(id)sender;
@end

@interface BaslAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation BaslAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}
@end

static void basl_gui_install_menu(NSApplication* app) {
    if (app == nil) {
        return;
    }
    if ([app mainMenu] != nil) {
        return;
    }

    NSMenu* menubar = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] initWithTitle:@""
                                                          action:nil
                                                   keyEquivalent:@""];
    [menubar addItem:appMenuItem];
    [app setMainMenu:menubar];

    NSString* appName = [[NSProcessInfo processInfo] processName];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:appName];
    NSString* quitTitle = [NSString stringWithFormat:@"Quit %@", appName];
    NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                      action:@selector(terminate:)
                                               keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [appMenuItem setSubmenu:appMenu];
}

static NSInteger basl_gui_clamp_index(NSInteger idx, NSInteger count) {
    if (count <= 0) {
        return -1;
    }
    if (idx < 0) {
        return 0;
    }
    if (idx >= count) {
        return count - 1;
    }
    return idx;
}

static void basl_gui_pin_child(NSView* container, NSView* child, CGFloat padding) {
    if (container == nil || child == nil) {
        return;
    }
    [child removeFromSuperview];
    [child setTranslatesAutoresizingMaskIntoConstraints:NO];
    [container addSubview:child];
    [NSLayoutConstraint activateConstraints:@[
        [child.leadingAnchor constraintEqualToAnchor:container.leadingAnchor constant:padding],
        [child.trailingAnchor constraintEqualToAnchor:container.trailingAnchor constant:-padding],
        [child.topAnchor constraintEqualToAnchor:container.topAnchor constant:padding],
        [child.bottomAnchor constraintEqualToAnchor:container.bottomAnchor constant:-padding],
    ]];
}

static void basl_gui_radio_apply_selection(NSStackView* group, NSInteger selectedIndex) {
    if (group == nil) {
        return;
    }
    NSArray* buttons = objc_getAssociatedObject(group, "basl_gui_radio_buttons");
    if (buttons == nil) {
        return;
    }
    NSInteger clamped = basl_gui_clamp_index(selectedIndex, [buttons count]);
    for (NSInteger i = 0; i < [buttons count]; i++) {
        id buttonObj = [buttons objectAtIndex:i];
        if (![buttonObj isKindOfClass:[NSButton class]]) {
            continue;
        }
        NSButton* button = (NSButton*)buttonObj;
        [button setState:(i == clamped ? NSControlStateValueOn : NSControlStateValueOff)];
    }
    if (clamped >= 0) {
        objc_setAssociatedObject(group, "basl_gui_radio_selected_index", @(clamped), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}

@implementation BaslRadioTarget
- (void)onSelect:(id)sender {
    if (self.group == nil || sender == nil || ![sender isKindOfClass:[NSButton class]]) {
        return;
    }
    NSArray* buttons = objc_getAssociatedObject(self.group, "basl_gui_radio_buttons");
    if (buttons == nil) {
        return;
    }
    NSInteger selected = -1;
    for (NSInteger i = 0; i < [buttons count]; i++) {
        if ([buttons objectAtIndex:i] == sender) {
            selected = i;
            break;
        }
    }
    if (selected >= 0) {
        basl_gui_radio_apply_selection(self.group, selected);
        if (self.callbackId != 0) {
            baslGuiInvokeCallback(self.callbackId);
        }
    }
}
@end

static char* basl_gui_strdup(const char* s) {
    if (s == NULL) {
        return NULL;
    }
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n + 1);
    return out;
}

static void basl_gui_set_error(char** errOut, const char* msg) {
    if (errOut == NULL) {
        return;
    }
    *errOut = basl_gui_strdup(msg);
}

void basl_gui_free_string(char* s) {
    if (s != NULL) {
        free(s);
    }
}

uintptr_t basl_gui_app_new(char** errOut) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        if (app == nil) {
            basl_gui_set_error(errOut, "failed to initialize NSApplication");
            return 0;
        }
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        BaslAppDelegate* delegate = [[BaslAppDelegate alloc] init];
        [app setDelegate:delegate];
        objc_setAssociatedObject(app, "basl_gui_app_delegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        basl_gui_install_menu(app);
        return (uintptr_t)(__bridge void*)app;
    }
}

int basl_gui_app_run(char** errOut) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        if (app == nil) {
            basl_gui_set_error(errOut, "NSApplication is not initialized");
            return 0;
        }
        [app run];
        return 1;
    }
}

int basl_gui_app_quit(char** errOut) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        if (app == nil) {
            basl_gui_set_error(errOut, "NSApplication is not initialized");
            return 0;
        }
        [app terminate:nil];
        return 1;
    }
}

uintptr_t basl_gui_window_new(const char* title, int32_t width, int32_t height, char** errOut) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        if (app == nil) {
            basl_gui_set_error(errOut, "NSApplication is not initialized");
            return 0;
        }
        NSRect rect = NSMakeRect(100, 100, width, height);
        NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
        NSWindow* window = [[NSWindow alloc] initWithContentRect:rect
                                                       styleMask:mask
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        if (window == nil) {
            basl_gui_set_error(errOut, "failed to create NSWindow");
            return 0;
        }
        NSString* t = [NSString stringWithUTF8String:(title != NULL ? title : "")];
        [window setTitle:t];
        [window setReleasedWhenClosed:NO];
        return (uintptr_t)(__bridge_retained void*)window;
    }
}

int basl_gui_window_set_title(uintptr_t windowPtr, const char* title, char** errOut) {
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)((void*)windowPtr);
        if (window == nil) {
            basl_gui_set_error(errOut, "invalid window handle");
            return 0;
        }
        NSString* t = [NSString stringWithUTF8String:(title != NULL ? title : "")];
        [window setTitle:t];
        return 1;
    }
}

int basl_gui_window_set_child(uintptr_t windowPtr, uintptr_t viewPtr, char** errOut) {
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)((void*)windowPtr);
        NSView* view = (__bridge NSView*)((void*)viewPtr);
        if (window == nil || view == nil) {
            basl_gui_set_error(errOut, "invalid window or view handle");
            return 0;
        }
        [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [window setContentView:view];
        return 1;
    }
}

int basl_gui_window_show(uintptr_t windowPtr, char** errOut) {
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)((void*)windowPtr);
        if (window == nil) {
            basl_gui_set_error(errOut, "invalid window handle");
            return 0;
        }
        [window makeKeyAndOrderFront:nil];
        [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
        return 1;
    }
}

int basl_gui_window_close(uintptr_t windowPtr, char** errOut) {
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)((void*)windowPtr);
        if (window == nil) {
            basl_gui_set_error(errOut, "invalid window handle");
            return 0;
        }
        [window close];
        return 1;
    }
}

uintptr_t basl_gui_box_new(int vertical, char** errOut) {
    @autoreleasepool {
        NSStackView* box = [[NSStackView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
        if (box == nil) {
            basl_gui_set_error(errOut, "failed to create NSStackView");
            return 0;
        }
        [box setOrientation:(vertical ? NSUserInterfaceLayoutOrientationVertical : NSUserInterfaceLayoutOrientationHorizontal)];
        [box setSpacing:8.0];
        [box setEdgeInsets:NSEdgeInsetsMake(12, 12, 12, 12)];
        [box setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        return (uintptr_t)(__bridge_retained void*)box;
    }
}

int basl_gui_box_add(uintptr_t boxPtr, uintptr_t childPtr, char** errOut) {
    @autoreleasepool {
        NSStackView* box = (__bridge NSStackView*)((void*)boxPtr);
        NSView* child = (__bridge NSView*)((void*)childPtr);
        if (box == nil || child == nil) {
            basl_gui_set_error(errOut, "invalid box or widget handle");
            return 0;
        }
        [box addArrangedSubview:child];
        return 1;
    }
}

int basl_gui_box_set_spacing(uintptr_t boxPtr, int32_t spacing, char** errOut) {
    @autoreleasepool {
        NSStackView* box = (__bridge NSStackView*)((void*)boxPtr);
        if (box == nil) {
            basl_gui_set_error(errOut, "invalid box handle");
            return 0;
        }
        [box setSpacing:(CGFloat)spacing];
        return 1;
    }
}

int basl_gui_box_set_padding(uintptr_t boxPtr, int32_t padding, char** errOut) {
    @autoreleasepool {
        NSStackView* box = (__bridge NSStackView*)((void*)boxPtr);
        if (box == nil) {
            basl_gui_set_error(errOut, "invalid box handle");
            return 0;
        }
        CGFloat p = (CGFloat)padding;
        [box setEdgeInsets:NSEdgeInsetsMake(p, p, p, p)];
        return 1;
    }
}

static NSView* basl_gui_grid_placeholder(void) {
    NSView* spacer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
    [spacer setHidden:YES];
    [spacer setTranslatesAutoresizingMaskIntoConstraints:NO];
    [[spacer.widthAnchor constraintGreaterThanOrEqualToConstant:1.0] setActive:YES];
    [[spacer.heightAnchor constraintGreaterThanOrEqualToConstant:1.0] setActive:YES];
    return spacer;
}

static int basl_gui_grid_ensure_size(NSGridView* grid, NSInteger targetRows, NSInteger targetCols, char** errOut) {
    if (targetRows <= 0 || targetCols <= 0) {
        basl_gui_set_error(errOut, "grid target size must be positive");
        return 0;
    }

    NSInteger rowCount = [grid numberOfRows];
    NSInteger colCount = [grid numberOfColumns];

    if (rowCount == 0 && colCount == 0) {
        NSMutableArray* firstRow = [NSMutableArray arrayWithCapacity:(NSUInteger)targetCols];
        for (NSInteger c = 0; c < targetCols; c++) {
            [firstRow addObject:basl_gui_grid_placeholder()];
        }
        [grid addRowWithViews:firstRow];
        rowCount = [grid numberOfRows];
        colCount = [grid numberOfColumns];
    }

    if (colCount == 0 && rowCount > 0) {
        NSMutableArray* column = [NSMutableArray arrayWithCapacity:(NSUInteger)rowCount];
        for (NSInteger r = 0; r < rowCount; r++) {
            [column addObject:basl_gui_grid_placeholder()];
        }
        [grid addColumnWithViews:column];
        colCount = [grid numberOfColumns];
    }

    while (colCount < targetCols) {
        NSMutableArray* column = [NSMutableArray arrayWithCapacity:(NSUInteger)rowCount];
        for (NSInteger r = 0; r < rowCount; r++) {
            [column addObject:basl_gui_grid_placeholder()];
        }
        [grid addColumnWithViews:column];
        colCount = [grid numberOfColumns];
    }

    while (rowCount < targetRows) {
        NSMutableArray* row = [NSMutableArray arrayWithCapacity:(NSUInteger)colCount];
        for (NSInteger c = 0; c < colCount; c++) {
            [row addObject:basl_gui_grid_placeholder()];
        }
        [grid addRowWithViews:row];
        rowCount = [grid numberOfRows];
    }

    return 1;
}

uintptr_t basl_gui_grid_new(int32_t rowSpacing, int32_t colSpacing, char** errOut) {
    @autoreleasepool {
        NSGridView* grid = [[NSGridView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
        if (grid == nil) {
            basl_gui_set_error(errOut, "failed to create NSGridView");
            return 0;
        }
        [grid setRowSpacing:(CGFloat)rowSpacing];
        [grid setColumnSpacing:(CGFloat)colSpacing];
        [grid setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        return (uintptr_t)(__bridge_retained void*)grid;
    }
}

int basl_gui_grid_place(uintptr_t gridPtr, uintptr_t childPtr, int32_t row, int32_t col, int32_t rowSpan, int32_t colSpan, int fillX, int fillY, char** errOut) {
    @autoreleasepool {
        NSGridView* grid = (__bridge NSGridView*)((void*)gridPtr);
        NSView* child = (__bridge NSView*)((void*)childPtr);
        if (grid == nil || child == nil) {
            basl_gui_set_error(errOut, "invalid grid or widget handle");
            return 0;
        }
        if (row < 0 || col < 0) {
            basl_gui_set_error(errOut, "grid row/col must be >= 0");
            return 0;
        }
        if (rowSpan <= 0 || colSpan <= 0) {
            basl_gui_set_error(errOut, "grid rowSpan/colSpan must be > 0");
            return 0;
        }

        NSInteger targetRows = (NSInteger)row + (NSInteger)rowSpan;
        NSInteger targetCols = (NSInteger)col + (NSInteger)colSpan;
        if (!basl_gui_grid_ensure_size(grid, targetRows, targetCols, errOut)) {
            return 0;
        }

        [child removeFromSuperview];
        NSGridCell* cell = [grid cellAtColumnIndex:(NSInteger)col rowIndex:(NSInteger)row];
        if (cell == nil) {
            basl_gui_set_error(errOut, "failed to resolve target grid cell");
            return 0;
        }

        if (rowSpan > 1 || colSpan > 1) {
            @try {
                [grid mergeCellsInHorizontalRange:NSMakeRange((NSUInteger)col, (NSUInteger)colSpan)
                                     verticalRange:NSMakeRange((NSUInteger)row, (NSUInteger)rowSpan)];
            } @catch (NSException* ex) {
                NSString* reason = [NSString stringWithFormat:@"failed to merge grid cells: %@", [ex reason]];
                basl_gui_set_error(errOut, [reason UTF8String]);
                return 0;
            }
            cell = [grid cellAtColumnIndex:(NSInteger)col rowIndex:(NSInteger)row];
            if (cell == nil) {
                basl_gui_set_error(errOut, "failed to resolve merged grid cell");
                return 0;
            }
        }

        [cell setContentView:child];
        [cell setXPlacement:(fillX ? NSGridCellPlacementFill : NSGridCellPlacementCenter)];
        [cell setYPlacement:(fillY ? NSGridCellPlacementFill : NSGridCellPlacementCenter)];
        return 1;
    }
}

uintptr_t basl_gui_label_new(const char* text, char** errOut) {
    @autoreleasepool {
        NSTextField* label = [NSTextField labelWithString:[NSString stringWithUTF8String:(text != NULL ? text : "")]];
        if (label == nil) {
            basl_gui_set_error(errOut, "failed to create label");
            return 0;
        }
        [label setSelectable:NO];
        return (uintptr_t)(__bridge_retained void*)label;
    }
}

int basl_gui_label_set_text(uintptr_t labelPtr, const char* text, char** errOut) {
    @autoreleasepool {
        NSTextField* label = (__bridge NSTextField*)((void*)labelPtr);
        if (label == nil) {
            basl_gui_set_error(errOut, "invalid label handle");
            return 0;
        }
        [label setStringValue:[NSString stringWithUTF8String:(text != NULL ? text : "")]];
        return 1;
    }
}

uintptr_t basl_gui_button_new(const char* text, char** errOut) {
    @autoreleasepool {
        NSButton* button = [NSButton buttonWithTitle:[NSString stringWithUTF8String:(text != NULL ? text : "")]
                                              target:nil
                                              action:nil];
        if (button == nil) {
            basl_gui_set_error(errOut, "failed to create button");
            return 0;
        }
        return (uintptr_t)(__bridge_retained void*)button;
    }
}

int basl_gui_button_set_text(uintptr_t buttonPtr, const char* text, char** errOut) {
    @autoreleasepool {
        NSButton* button = (__bridge NSButton*)((void*)buttonPtr);
        if (button == nil) {
            basl_gui_set_error(errOut, "invalid button handle");
            return 0;
        }
        [button setTitle:[NSString stringWithUTF8String:(text != NULL ? text : "")]];
        return 1;
    }
}

int basl_gui_button_set_on_click(uintptr_t buttonPtr, uintptr_t callbackId, char** errOut) {
    @autoreleasepool {
        NSButton* button = (__bridge NSButton*)((void*)buttonPtr);
        if (button == nil) {
            basl_gui_set_error(errOut, "invalid button handle");
            return 0;
        }
        BaslControlTarget* target = [[BaslControlTarget alloc] init];
        if (target == nil) {
            basl_gui_set_error(errOut, "failed to allocate button target");
            return 0;
        }
        target.callbackId = callbackId;
        [button setTarget:target];
        [button setAction:@selector(onAction:)];
        objc_setAssociatedObject(button, "basl_gui_button_target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return 1;
    }
}

uintptr_t basl_gui_entry_new(char** errOut) {
    @autoreleasepool {
        NSTextField* entry = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
        if (entry == nil) {
            basl_gui_set_error(errOut, "failed to create text entry");
            return 0;
        }
        return (uintptr_t)(__bridge_retained void*)entry;
    }
}

char* basl_gui_entry_text(uintptr_t entryPtr, char** errOut) {
    @autoreleasepool {
        NSTextField* entry = (__bridge NSTextField*)((void*)entryPtr);
        if (entry == nil) {
            basl_gui_set_error(errOut, "invalid entry handle");
            return NULL;
        }
        const char* raw = [[entry stringValue] UTF8String];
        return basl_gui_strdup(raw != NULL ? raw : "");
    }
}

int basl_gui_entry_set_text(uintptr_t entryPtr, const char* text, char** errOut) {
    @autoreleasepool {
        NSTextField* entry = (__bridge NSTextField*)((void*)entryPtr);
        if (entry == nil) {
            basl_gui_set_error(errOut, "invalid entry handle");
            return 0;
        }
        [entry setStringValue:[NSString stringWithUTF8String:(text != NULL ? text : "")]];
        return 1;
    }
}

uintptr_t basl_gui_checkbox_new(const char* text, int checked, char** errOut) {
    @autoreleasepool {
        NSButton* checkbox = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
        if (checkbox == nil) {
            basl_gui_set_error(errOut, "failed to create checkbox");
            return 0;
        }
        [checkbox setButtonType:NSButtonTypeSwitch];
        [checkbox setTitle:[NSString stringWithUTF8String:(text != NULL ? text : "")]];
        [checkbox setState:(checked ? NSControlStateValueOn : NSControlStateValueOff)];
        return (uintptr_t)(__bridge_retained void*)checkbox;
    }
}

int basl_gui_checkbox_set_text(uintptr_t checkboxPtr, const char* text, char** errOut) {
    @autoreleasepool {
        NSButton* checkbox = (__bridge NSButton*)((void*)checkboxPtr);
        if (checkbox == nil) {
            basl_gui_set_error(errOut, "invalid checkbox handle");
            return 0;
        }
        [checkbox setTitle:[NSString stringWithUTF8String:(text != NULL ? text : "")]];
        return 1;
    }
}

int basl_gui_checkbox_is_checked(uintptr_t checkboxPtr, int* checkedOut, char** errOut) {
    @autoreleasepool {
        NSButton* checkbox = (__bridge NSButton*)((void*)checkboxPtr);
        if (checkbox == nil || checkedOut == NULL) {
            basl_gui_set_error(errOut, "invalid checkbox handle");
            return 0;
        }
        *checkedOut = ([checkbox state] == NSControlStateValueOn) ? 1 : 0;
        return 1;
    }
}

int basl_gui_checkbox_set_checked(uintptr_t checkboxPtr, int checked, char** errOut) {
    @autoreleasepool {
        NSButton* checkbox = (__bridge NSButton*)((void*)checkboxPtr);
        if (checkbox == nil) {
            basl_gui_set_error(errOut, "invalid checkbox handle");
            return 0;
        }
        [checkbox setState:(checked ? NSControlStateValueOn : NSControlStateValueOff)];
        return 1;
    }
}

int basl_gui_checkbox_set_on_toggle(uintptr_t checkboxPtr, uintptr_t callbackId, char** errOut) {
    @autoreleasepool {
        NSButton* checkbox = (__bridge NSButton*)((void*)checkboxPtr);
        if (checkbox == nil) {
            basl_gui_set_error(errOut, "invalid checkbox handle");
            return 0;
        }
        BaslControlTarget* target = [[BaslControlTarget alloc] init];
        if (target == nil) {
            basl_gui_set_error(errOut, "failed to allocate checkbox target");
            return 0;
        }
        target.callbackId = callbackId;
        [checkbox setTarget:target];
        [checkbox setAction:@selector(onAction:)];
        objc_setAssociatedObject(checkbox, "basl_gui_checkbox_target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return 1;
    }
}

uintptr_t basl_gui_select_new(const char** items, int32_t itemCount, int32_t selectedIndex, char** errOut) {
    @autoreleasepool {
        if (itemCount < 0) {
            basl_gui_set_error(errOut, "itemCount must be >= 0");
            return 0;
        }
        NSPopUpButton* select = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 240, 26) pullsDown:NO];
        if (select == nil) {
            basl_gui_set_error(errOut, "failed to create select");
            return 0;
        }
        [select removeAllItems];
        for (int32_t i = 0; i < itemCount; i++) {
            const char* raw = (items != NULL && items[i] != NULL) ? items[i] : "";
            [select addItemWithTitle:[NSString stringWithUTF8String:raw]];
        }
        NSInteger count = [select numberOfItems];
        if (count > 0) {
            NSInteger idx = (NSInteger)selectedIndex;
            if (idx < 0) {
                idx = 0;
            }
            if (idx >= count) {
                idx = count - 1;
            }
            [select selectItemAtIndex:idx];
        }
        return (uintptr_t)(__bridge_retained void*)select;
    }
}

int basl_gui_select_selected_index(uintptr_t selectPtr, int32_t* outIndex, char** errOut) {
    @autoreleasepool {
        NSPopUpButton* select = (__bridge NSPopUpButton*)((void*)selectPtr);
        if (select == nil || outIndex == NULL) {
            basl_gui_set_error(errOut, "invalid select handle");
            return 0;
        }
        if ([select numberOfItems] == 0) {
            *outIndex = -1;
            return 1;
        }
        *outIndex = (int32_t)[select indexOfSelectedItem];
        return 1;
    }
}

int basl_gui_select_set_selected_index(uintptr_t selectPtr, int32_t selectedIndex, char** errOut) {
    @autoreleasepool {
        NSPopUpButton* select = (__bridge NSPopUpButton*)((void*)selectPtr);
        if (select == nil) {
            basl_gui_set_error(errOut, "invalid select handle");
            return 0;
        }
        NSInteger count = [select numberOfItems];
        if (count == 0) {
            basl_gui_set_error(errOut, "select has no items");
            return 0;
        }
        if (selectedIndex < 0 || selectedIndex >= count) {
            basl_gui_set_error(errOut, "selected index out of range");
            return 0;
        }
        [select selectItemAtIndex:(NSInteger)selectedIndex];
        return 1;
    }
}

char* basl_gui_select_selected_text(uintptr_t selectPtr, char** errOut) {
    @autoreleasepool {
        NSPopUpButton* select = (__bridge NSPopUpButton*)((void*)selectPtr);
        if (select == nil) {
            basl_gui_set_error(errOut, "invalid select handle");
            return NULL;
        }
        NSString* text = [select titleOfSelectedItem];
        if (text == nil) {
            return basl_gui_strdup("");
        }
        return basl_gui_strdup([text UTF8String]);
    }
}

int basl_gui_select_add_item(uintptr_t selectPtr, const char* text, char** errOut) {
    @autoreleasepool {
        NSPopUpButton* select = (__bridge NSPopUpButton*)((void*)selectPtr);
        if (select == nil) {
            basl_gui_set_error(errOut, "invalid select handle");
            return 0;
        }
        [select addItemWithTitle:[NSString stringWithUTF8String:(text != NULL ? text : "")]];
        return 1;
    }
}

int basl_gui_select_set_on_change(uintptr_t selectPtr, uintptr_t callbackId, char** errOut) {
    @autoreleasepool {
        NSPopUpButton* select = (__bridge NSPopUpButton*)((void*)selectPtr);
        if (select == nil) {
            basl_gui_set_error(errOut, "invalid select handle");
            return 0;
        }
        BaslControlTarget* target = [[BaslControlTarget alloc] init];
        if (target == nil) {
            basl_gui_set_error(errOut, "failed to allocate select target");
            return 0;
        }
        target.callbackId = callbackId;
        [select setTarget:target];
        [select setAction:@selector(onAction:)];
        objc_setAssociatedObject(select, "basl_gui_select_target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return 1;
    }
}

static NSTextView* basl_gui_textarea_resolve(uintptr_t textareaPtr, char** errOut) {
    NSScrollView* scroll = (__bridge NSScrollView*)((void*)textareaPtr);
    if (scroll == nil) {
        basl_gui_set_error(errOut, "invalid text area handle");
        return nil;
    }
    id doc = [scroll documentView];
    if (doc == nil || ![doc isKindOfClass:[NSTextView class]]) {
        basl_gui_set_error(errOut, "text area document view is unavailable");
        return nil;
    }
    return (NSTextView*)doc;
}

uintptr_t basl_gui_textarea_new(char** errOut) {
    @autoreleasepool {
        NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 420, 160)];
        if (scroll == nil) {
            basl_gui_set_error(errOut, "failed to create text area scroll view");
            return 0;
        }
        [scroll setHasVerticalScroller:YES];
        [scroll setHasHorizontalScroller:NO];
        [scroll setBorderType:NSBezelBorder];

        NSTextView* textView = [[NSTextView alloc] initWithFrame:[scroll bounds]];
        if (textView == nil) {
            basl_gui_set_error(errOut, "failed to create text area view");
            return 0;
        }
        [textView setMinSize:NSMakeSize(0, 0)];
        [textView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
        [textView setVerticallyResizable:YES];
        [textView setHorizontallyResizable:NO];
        [[textView textContainer] setContainerSize:NSMakeSize([scroll contentSize].width, FLT_MAX)];
        [[textView textContainer] setWidthTracksTextView:YES];

        [scroll setDocumentView:textView];
        return (uintptr_t)(__bridge_retained void*)scroll;
    }
}

char* basl_gui_textarea_text(uintptr_t textareaPtr, char** errOut) {
    @autoreleasepool {
        NSTextView* textView = basl_gui_textarea_resolve(textareaPtr, errOut);
        if (textView == nil) {
            return NULL;
        }
        NSString* text = [textView string];
        return basl_gui_strdup([[text != nil ? text : @"" description] UTF8String]);
    }
}

int basl_gui_textarea_set_text(uintptr_t textareaPtr, const char* text, char** errOut) {
    @autoreleasepool {
        NSTextView* textView = basl_gui_textarea_resolve(textareaPtr, errOut);
        if (textView == nil) {
            return 0;
        }
        [textView setString:[NSString stringWithUTF8String:(text != NULL ? text : "")]];
        return 1;
    }
}

int basl_gui_textarea_append(uintptr_t textareaPtr, const char* text, char** errOut) {
    @autoreleasepool {
        NSTextView* textView = basl_gui_textarea_resolve(textareaPtr, errOut);
        if (textView == nil) {
            return 0;
        }
        NSString* suffix = [NSString stringWithUTF8String:(text != NULL ? text : "")];
        NSString* current = [textView string];
        if (current == nil) {
            current = @"";
        }
        [textView setString:[current stringByAppendingString:suffix]];
        return 1;
    }
}

uintptr_t basl_gui_progress_new(double minValue, double maxValue, double value, int indeterminate, char** errOut) {
    @autoreleasepool {
        if (!indeterminate && maxValue <= minValue) {
            basl_gui_set_error(errOut, "progress max must be greater than min");
            return 0;
        }
        NSProgressIndicator* progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 200, 20)];
        if (progress == nil) {
            basl_gui_set_error(errOut, "failed to create progress indicator");
            return 0;
        }
        [progress setStyle:NSProgressIndicatorStyleBar];
        [progress setIndeterminate:(indeterminate ? YES : NO)];
        if (indeterminate) {
            [progress startAnimation:nil];
        } else {
            [progress setMinValue:minValue];
            [progress setMaxValue:maxValue];
            if (value < minValue) {
                value = minValue;
            }
            if (value > maxValue) {
                value = maxValue;
            }
            [progress setDoubleValue:value];
        }
        return (uintptr_t)(__bridge_retained void*)progress;
    }
}

int basl_gui_progress_value(uintptr_t progressPtr, double* outValue, char** errOut) {
    @autoreleasepool {
        NSProgressIndicator* progress = (__bridge NSProgressIndicator*)((void*)progressPtr);
        if (progress == nil || outValue == NULL) {
            basl_gui_set_error(errOut, "invalid progress handle");
            return 0;
        }
        *outValue = [progress doubleValue];
        return 1;
    }
}

int basl_gui_progress_set_value(uintptr_t progressPtr, double value, char** errOut) {
    @autoreleasepool {
        NSProgressIndicator* progress = (__bridge NSProgressIndicator*)((void*)progressPtr);
        if (progress == nil) {
            basl_gui_set_error(errOut, "invalid progress handle");
            return 0;
        }
        if (![progress isIndeterminate]) {
            double minValue = [progress minValue];
            double maxValue = [progress maxValue];
            if (value < minValue) {
                value = minValue;
            }
            if (value > maxValue) {
                value = maxValue;
            }
        }
        [progress setDoubleValue:value];
        return 1;
    }
}

uintptr_t basl_gui_frame_new(int32_t padding, char** errOut) {
    @autoreleasepool {
        NSView* frame = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 200, 200)];
        if (frame == nil) {
            basl_gui_set_error(errOut, "failed to create frame");
            return 0;
        }
        objc_setAssociatedObject(frame, "basl_gui_frame_padding", @(padding), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return (uintptr_t)(__bridge_retained void*)frame;
    }
}

int basl_gui_frame_set_child(uintptr_t framePtr, uintptr_t childPtr, char** errOut) {
    @autoreleasepool {
        NSView* frame = (__bridge NSView*)((void*)framePtr);
        NSView* child = (__bridge NSView*)((void*)childPtr);
        if (frame == nil || child == nil) {
            basl_gui_set_error(errOut, "invalid frame or child handle");
            return 0;
        }
        NSNumber* paddingNum = objc_getAssociatedObject(frame, "basl_gui_frame_padding");
        CGFloat padding = paddingNum != nil ? [paddingNum doubleValue] : 0.0;
        NSArray* existing = [[frame subviews] copy];
        for (NSView* v in existing) {
            [v removeFromSuperview];
        }
        basl_gui_pin_child(frame, child, padding);
        return 1;
    }
}

uintptr_t basl_gui_group_new(const char* title, int32_t padding, char** errOut) {
    @autoreleasepool {
        NSBox* group = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, 260, 180)];
        if (group == nil) {
            basl_gui_set_error(errOut, "failed to create group");
            return 0;
        }
        [group setTitle:[NSString stringWithUTF8String:(title != NULL ? title : "")]];
        [group setBoxType:NSBoxPrimary];
        objc_setAssociatedObject(group, "basl_gui_group_padding", @(padding), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return (uintptr_t)(__bridge_retained void*)group;
    }
}

int basl_gui_group_set_child(uintptr_t groupPtr, uintptr_t childPtr, char** errOut) {
    @autoreleasepool {
        NSBox* group = (__bridge NSBox*)((void*)groupPtr);
        NSView* child = (__bridge NSView*)((void*)childPtr);
        if (group == nil || child == nil) {
            basl_gui_set_error(errOut, "invalid group or child handle");
            return 0;
        }
        NSNumber* paddingNum = objc_getAssociatedObject(group, "basl_gui_group_padding");
        CGFloat padding = paddingNum != nil ? [paddingNum doubleValue] : 0.0;
        NSView* wrapper = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 200, 120)];
        if (wrapper == nil) {
            basl_gui_set_error(errOut, "failed to allocate group content view");
            return 0;
        }
        basl_gui_pin_child(wrapper, child, padding);
        [group setContentView:wrapper];
        return 1;
    }
}

int basl_gui_group_set_title(uintptr_t groupPtr, const char* title, char** errOut) {
    @autoreleasepool {
        NSBox* group = (__bridge NSBox*)((void*)groupPtr);
        if (group == nil) {
            basl_gui_set_error(errOut, "invalid group handle");
            return 0;
        }
        [group setTitle:[NSString stringWithUTF8String:(title != NULL ? title : "")]];
        return 1;
    }
}

uintptr_t basl_gui_radio_new(const char** items, int32_t itemCount, int32_t selectedIndex, int vertical, char** errOut) {
    @autoreleasepool {
        if (itemCount < 0) {
            basl_gui_set_error(errOut, "itemCount must be >= 0");
            return 0;
        }
        NSStackView* group = [[NSStackView alloc] initWithFrame:NSMakeRect(0, 0, 220, 120)];
        if (group == nil) {
            basl_gui_set_error(errOut, "failed to create radio group");
            return 0;
        }
        [group setOrientation:(vertical ? NSUserInterfaceLayoutOrientationVertical : NSUserInterfaceLayoutOrientationHorizontal)];
        [group setSpacing:6.0];

        BaslRadioTarget* target = [[BaslRadioTarget alloc] init];
        if (target == nil) {
            basl_gui_set_error(errOut, "failed to allocate radio target");
            return 0;
        }
        target.group = group;
        target.callbackId = 0;

        NSMutableArray* buttons = [NSMutableArray arrayWithCapacity:(NSUInteger)itemCount];
        for (int32_t i = 0; i < itemCount; i++) {
            const char* raw = (items != NULL && items[i] != NULL) ? items[i] : "";
            NSButton* button = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 180, 22)];
            if (button == nil) {
                basl_gui_set_error(errOut, "failed to create radio button");
                return 0;
            }
            [button setButtonType:NSButtonTypeRadio];
            [button setTitle:[NSString stringWithUTF8String:raw]];
            [button setTarget:target];
            [button setAction:@selector(onSelect:)];
            [buttons addObject:button];
            [group addArrangedSubview:button];
        }

        objc_setAssociatedObject(group, "basl_gui_radio_buttons", buttons, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(group, "basl_gui_radio_target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        basl_gui_radio_apply_selection(group, selectedIndex);
        return (uintptr_t)(__bridge_retained void*)group;
    }
}

int basl_gui_radio_selected_index(uintptr_t radioPtr, int32_t* outIndex, char** errOut) {
    @autoreleasepool {
        NSStackView* group = (__bridge NSStackView*)((void*)radioPtr);
        if (group == nil || outIndex == NULL) {
            basl_gui_set_error(errOut, "invalid radio handle");
            return 0;
        }
        NSNumber* selectedNum = objc_getAssociatedObject(group, "basl_gui_radio_selected_index");
        if (selectedNum == nil) {
            *outIndex = -1;
            return 1;
        }
        *outIndex = (int32_t)[selectedNum integerValue];
        return 1;
    }
}

int basl_gui_radio_set_selected_index(uintptr_t radioPtr, int32_t selectedIndex, char** errOut) {
    @autoreleasepool {
        NSStackView* group = (__bridge NSStackView*)((void*)radioPtr);
        if (group == nil) {
            basl_gui_set_error(errOut, "invalid radio handle");
            return 0;
        }
        NSArray* buttons = objc_getAssociatedObject(group, "basl_gui_radio_buttons");
        if (buttons == nil || [buttons count] == 0) {
            basl_gui_set_error(errOut, "radio group has no options");
            return 0;
        }
        if (selectedIndex < 0 || selectedIndex >= [buttons count]) {
            basl_gui_set_error(errOut, "radio selected index out of range");
            return 0;
        }
        basl_gui_radio_apply_selection(group, selectedIndex);
        return 1;
    }
}

char* basl_gui_radio_selected_text(uintptr_t radioPtr, char** errOut) {
    @autoreleasepool {
        NSStackView* group = (__bridge NSStackView*)((void*)radioPtr);
        if (group == nil) {
            basl_gui_set_error(errOut, "invalid radio handle");
            return NULL;
        }
        NSArray* buttons = objc_getAssociatedObject(group, "basl_gui_radio_buttons");
        NSNumber* selectedNum = objc_getAssociatedObject(group, "basl_gui_radio_selected_index");
        if (buttons == nil || selectedNum == nil) {
            return basl_gui_strdup("");
        }
        NSInteger idx = [selectedNum integerValue];
        if (idx < 0 || idx >= [buttons count]) {
            return basl_gui_strdup("");
        }
        id buttonObj = [buttons objectAtIndex:idx];
        if (![buttonObj isKindOfClass:[NSButton class]]) {
            return basl_gui_strdup("");
        }
        NSString* title = [(NSButton*)buttonObj title];
        return basl_gui_strdup([(title != nil ? title : @"") UTF8String]);
    }
}

int basl_gui_radio_set_on_change(uintptr_t radioPtr, uintptr_t callbackId, char** errOut) {
    @autoreleasepool {
        NSStackView* group = (__bridge NSStackView*)((void*)radioPtr);
        if (group == nil) {
            basl_gui_set_error(errOut, "invalid radio handle");
            return 0;
        }
        id targetObj = objc_getAssociatedObject(group, "basl_gui_radio_target");
        if (targetObj == nil || ![targetObj isKindOfClass:[BaslRadioTarget class]]) {
            basl_gui_set_error(errOut, "radio target is unavailable");
            return 0;
        }
        BaslRadioTarget* target = (BaslRadioTarget*)targetObj;
        target.callbackId = callbackId;
        return 1;
    }
}

uintptr_t basl_gui_scale_new(double minValue, double maxValue, double value, int vertical, char** errOut) {
    @autoreleasepool {
        if (maxValue <= minValue) {
            basl_gui_set_error(errOut, "scale max must be greater than min");
            return 0;
        }
        NSSlider* scale = [[NSSlider alloc] initWithFrame:NSMakeRect(0, 0, 220, 24)];
        if (scale == nil) {
            basl_gui_set_error(errOut, "failed to create scale");
            return 0;
        }
        [scale setMinValue:minValue];
        [scale setMaxValue:maxValue];
        if (value < minValue) {
            value = minValue;
        }
        if (value > maxValue) {
            value = maxValue;
        }
        [scale setDoubleValue:value];
        [scale setVertical:(vertical ? YES : NO)];
        return (uintptr_t)(__bridge_retained void*)scale;
    }
}

int basl_gui_scale_value(uintptr_t scalePtr, double* outValue, char** errOut) {
    @autoreleasepool {
        NSSlider* scale = (__bridge NSSlider*)((void*)scalePtr);
        if (scale == nil || outValue == NULL) {
            basl_gui_set_error(errOut, "invalid scale handle");
            return 0;
        }
        *outValue = [scale doubleValue];
        return 1;
    }
}

int basl_gui_scale_set_value(uintptr_t scalePtr, double value, char** errOut) {
    @autoreleasepool {
        NSSlider* scale = (__bridge NSSlider*)((void*)scalePtr);
        if (scale == nil) {
            basl_gui_set_error(errOut, "invalid scale handle");
            return 0;
        }
        double minValue = [scale minValue];
        double maxValue = [scale maxValue];
        if (value < minValue) {
            value = minValue;
        }
        if (value > maxValue) {
            value = maxValue;
        }
        [scale setDoubleValue:value];
        return 1;
    }
}

int basl_gui_scale_set_on_change(uintptr_t scalePtr, uintptr_t callbackId, char** errOut) {
    @autoreleasepool {
        NSSlider* scale = (__bridge NSSlider*)((void*)scalePtr);
        if (scale == nil) {
            basl_gui_set_error(errOut, "invalid scale handle");
            return 0;
        }
        BaslControlTarget* target = [[BaslControlTarget alloc] init];
        if (target == nil) {
            basl_gui_set_error(errOut, "failed to allocate scale target");
            return 0;
        }
        target.callbackId = callbackId;
        [scale setTarget:target];
        [scale setAction:@selector(onAction:)];
        objc_setAssociatedObject(scale, "basl_gui_scale_target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return 1;
    }
}

uintptr_t basl_gui_spinbox_new(double minValue, double maxValue, double step, double value, char** errOut) {
    @autoreleasepool {
        if (maxValue <= minValue) {
            basl_gui_set_error(errOut, "spinbox max must be greater than min");
            return 0;
        }
        if (step <= 0) {
            basl_gui_set_error(errOut, "spinbox step must be > 0");
            return 0;
        }
        NSStepper* spinbox = [[NSStepper alloc] initWithFrame:NSMakeRect(0, 0, 120, 24)];
        if (spinbox == nil) {
            basl_gui_set_error(errOut, "failed to create spinbox");
            return 0;
        }
        [spinbox setMinValue:minValue];
        [spinbox setMaxValue:maxValue];
        [spinbox setIncrement:step];
        if (value < minValue) {
            value = minValue;
        }
        if (value > maxValue) {
            value = maxValue;
        }
        [spinbox setDoubleValue:value];
        [spinbox setValueWraps:NO];
        return (uintptr_t)(__bridge_retained void*)spinbox;
    }
}

int basl_gui_spinbox_value(uintptr_t spinboxPtr, double* outValue, char** errOut) {
    @autoreleasepool {
        NSStepper* spinbox = (__bridge NSStepper*)((void*)spinboxPtr);
        if (spinbox == nil || outValue == NULL) {
            basl_gui_set_error(errOut, "invalid spinbox handle");
            return 0;
        }
        *outValue = [spinbox doubleValue];
        return 1;
    }
}

int basl_gui_spinbox_set_value(uintptr_t spinboxPtr, double value, char** errOut) {
    @autoreleasepool {
        NSStepper* spinbox = (__bridge NSStepper*)((void*)spinboxPtr);
        if (spinbox == nil) {
            basl_gui_set_error(errOut, "invalid spinbox handle");
            return 0;
        }
        double minValue = [spinbox minValue];
        double maxValue = [spinbox maxValue];
        if (value < minValue) {
            value = minValue;
        }
        if (value > maxValue) {
            value = maxValue;
        }
        [spinbox setDoubleValue:value];
        return 1;
    }
}

int basl_gui_spinbox_set_on_change(uintptr_t spinboxPtr, uintptr_t callbackId, char** errOut) {
    @autoreleasepool {
        NSStepper* spinbox = (__bridge NSStepper*)((void*)spinboxPtr);
        if (spinbox == nil) {
            basl_gui_set_error(errOut, "invalid spinbox handle");
            return 0;
        }
        BaslControlTarget* target = [[BaslControlTarget alloc] init];
        if (target == nil) {
            basl_gui_set_error(errOut, "failed to allocate spinbox target");
            return 0;
        }
        target.callbackId = callbackId;
        [spinbox setTarget:target];
        [spinbox setAction:@selector(onAction:)];
        objc_setAssociatedObject(spinbox, "basl_gui_spinbox_target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return 1;
    }
}

uintptr_t basl_gui_separator_new(int vertical, char** errOut) {
    @autoreleasepool {
        NSBox* separator = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, (vertical ? 1 : 160), (vertical ? 120 : 1))];
        if (separator == nil) {
            basl_gui_set_error(errOut, "failed to create separator");
            return 0;
        }
        [separator setBoxType:NSBoxSeparator];
        return (uintptr_t)(__bridge_retained void*)separator;
    }
}

int basl_gui_widget_set_size(uintptr_t viewPtr, int32_t width, int32_t height, char** errOut) {
    @autoreleasepool {
        NSView* view = (__bridge NSView*)((void*)viewPtr);
        if (view == nil) {
            basl_gui_set_error(errOut, "invalid widget handle");
            return 0;
        }
        [view setTranslatesAutoresizingMaskIntoConstraints:NO];
        if (width > 0) {
            [[view.widthAnchor constraintEqualToConstant:(CGFloat)width] setActive:YES];
        }
        if (height > 0) {
            [[view.heightAnchor constraintEqualToConstant:(CGFloat)height] setActive:YES];
        }
        return 1;
    }
}
