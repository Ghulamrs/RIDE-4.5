// RIDE's window on macOS: an AppKit front end over the same core the terminal
// editor and the Windows Forms window use. `RIDE --version` answers without a
// window; otherwise the arguments are a project (.pro or its directory) and
// files to open, as with the other two.
#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <cstring>

#import "RIDEStrings.h"
#import "RIDEWindowController.h"

@interface RIDEAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) RIDEWindowController* window;
@property(nonatomic, copy) NSString* project;
@property(nonatomic, strong) NSMutableArray<NSString*>* files;
@end

@implementation RIDEAppDelegate

- (void)applicationWillFinishLaunching:(NSNotification*)note {
    (void)note;
    self.window = [[RIDEWindowController alloc] init];
    NSApp.mainMenu = [self.window makeMainMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    (void)note;
    [self.window showWindow:nil];
    [self.window.window makeKeyAndOrderFront:nil];
    [self.window startWithProject:self.project files:self.files];
    [NSApp activateIgnoringOtherApps:YES];
}

// Files dropped on the Dock icon, or opened with RIDE from Finder.
- (void)application:(NSApplication*)sender openFiles:(NSArray<NSString*>*)paths {
    NSString* suffix = RIDEStr(ride_project_suffix());
    for (NSString* path in paths) {
        BOOL directory = NO;
        [NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&directory];
        BOOL project = directory || (suffix.length > 0 && [path hasSuffix:suffix]);
        if (self.window.window.isVisible) {
            if (project) [self.window loadProject:path];
            else [self.window openPath:path];
        } else if (project) {
            self.project = path;
        } else {
            [self.files addObject:path];
        }
    }
    [sender replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    return [self.window mayClose] ? NSTerminateNow : NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app {
    (void)app;
    return YES;
}

@end

int main(int argc, const char* argv[]) {
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("%s %s\n", ride_product_name(), ride_version());
        return 0;
    }

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        RIDEAppDelegate* delegate = [[RIDEAppDelegate alloc] init];
        delegate.files = [NSMutableArray array];

        // Arguments as the other two front ends take them; the ones macOS
        // itself adds (-psn_..., -NSDocumentRevisionsDebugMode YES) are skipped.
        NSString* here = NSFileManager.defaultManager.currentDirectoryPath;
        for (int i = 1; i < argc; ++i) {
            const char* word = argv[i];
            if (word[0] == '-') {
                if (std::strcmp(word, "--project") == 0 && i + 1 < argc) {
                    delegate.project = RIDEStr(argv[++i]);
                    continue;
                }
                if (std::strncmp(word, "-NS", 3) == 0 || std::strncmp(word, "-Apple", 6) == 0) ++i;
                continue;
            }
            NSString* path = RIDEStr(word);
            if (!path.isAbsolutePath) path = [here stringByAppendingPathComponent:path];
            [delegate.files addObject:path.stringByStandardizingPath];
        }

        app.delegate = delegate;
        [app run];
    }
    return 0;
}
