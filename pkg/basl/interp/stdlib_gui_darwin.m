#include <Cocoa/Cocoa.h>
#include <objc/runtime.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void baslGuiInvokeCallback(uintptr_t callbackId);

@interface BaslButtonTarget : NSObject
@property(nonatomic, assign) uintptr_t callbackId;
- (void)onClick:(id)sender;
@end

@implementation BaslButtonTarget
- (void)onClick:(id)sender {
    baslGuiInvokeCallback(self.callbackId);
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
        [app stop:nil];
        NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                           location:NSMakePoint(0, 0)
                                      modifierFlags:0
                                          timestamp:0
                                       windowNumber:0
                                            context:nil
                                            subtype:0
                                              data1:0
                                              data2:0];
        [app postEvent:wake atStart:NO];
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

int basl_gui_grid_place(uintptr_t gridPtr, uintptr_t childPtr, int32_t row, int32_t col, int32_t rowSpan, int32_t colSpan, char** errOut) {
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
        [cell setXPlacement:NSGridCellPlacementFill];
        [cell setYPlacement:NSGridCellPlacementFill];
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
        BaslButtonTarget* target = [[BaslButtonTarget alloc] init];
        if (target == nil) {
            basl_gui_set_error(errOut, "failed to allocate button target");
            return 0;
        }
        target.callbackId = callbackId;
        [button setTarget:target];
        [button setAction:@selector(onClick:)];
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
