// The window: the source files down the left, the file in the middle, the
// Errors / Progress / Output panel across the bottom quarter, and the one-line
// status bar under everything. It consumes the native core through
// winforms/bridge.h exactly as the Windows Forms window does - laying a file
// out, colouring it, reading the project and choosing and running the
// compilers are the same code - so the two windows cannot drift apart on
// anything but looks.
#ifndef MACOS_WINDOW_CONTROLLER_H
#define MACOS_WINDOW_CONTROLLER_H

#import <Cocoa/Cocoa.h>

#import "CodeView.h"

@interface WindowController
    : NSWindowController <NSWindowDelegate, NSTextViewDelegate, NSOutlineViewDataSource,
                          NSOutlineViewDelegate, NSTableViewDataSource, NSTableViewDelegate,
                          NSSplitViewDelegate, NSMenuDelegate, NSMenuItemValidation,
                          CodeViewHost>

- (instancetype)init;

// A project directory or .pro to open, and files named on the command line.
- (void)startWithProject:(NSString*)project files:(NSArray<NSString*>*)files;

- (void)openPath:(NSString*)path;
- (void)loadProject:(NSString*)where;

// Asks about every unsaved file; NO when the person cancelled.
- (BOOL)mayClose;

// The main menu, built once: File, Edit, View, Project, Build, Target, Option,
// Help - with the application and Window menus macOS expects around them.
- (NSMenu*)makeMainMenu;

@end

#endif
