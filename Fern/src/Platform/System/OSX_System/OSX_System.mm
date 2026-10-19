#import "OSX_System.hpp"
#import "AppDelegate.h"
#import "CocoaWindow/CocoaWindow.h"
#import "CocoaWindow/WonderView.h"

#import <Foundation/Allocator.hpp>
#import <Foundation/IO/FileStreamReader.hpp>
#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <sys/param.h>
#include <unistd.h>

@interface MainHelper : NSObject
@end

@implementation MainHelper
- (void)selectedKeyboardInputSourceChanged:(NSObject *)object {}
- (void)doNothing:(id)object {}
@end

namespace Fern {

static NSCursor *s_cursors[Cursor::COUNT];

namespace {

std::filesystem::path findBundleResourcesPath() {
    char resourcesPath[MAXPATHLEN];
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle) { return {}; }

    CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(bundle);
    if (!resourcesURL) { return {}; }

    CFStringRef last = CFURLCopyLastPathComponent(resourcesURL);
    if (!last || CFStringCompare(CFSTR("Resources"), last, 0) != kCFCompareEqualTo) {
        if (last) { CFRelease(last); }
        CFRelease(resourcesURL);
        return {};
    }
    CFRelease(last);

    if (!CFURLGetFileSystemRepresentation(resourcesURL, true, reinterpret_cast<UInt8 *>(resourcesPath), MAXPATHLEN)) {
        CFRelease(resourcesURL);
        return {};
    }

    CFRelease(resourcesURL);
    return std::filesystem::path(resourcesPath);
}

void changeToResourcesDirectory(const std::filesystem::path &resourcesPath) {
    if (resourcesPath.empty()) { return; }
    chdir(resourcesPath.string().c_str());
}

void installMainMenu(NSApplication *application) {
    NSMenu *menubar = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    NSMenuItem *appMenuItem = [[[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""] autorelease];
    [menubar addItem:appMenuItem];

    NSMenu *appMenu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    NSMenuItem *quitMenuItem = [[[NSMenuItem alloc] initWithTitle:@"Quit"
                                                           action:@selector(terminate:)
                                                    keyEquivalent:@"q"] autorelease];
    [appMenu addItem:quitMenuItem];
    [appMenuItem setSubmenu:appMenu];
    [application setMainMenu:menubar];
}

void bootstrapCocoaApplication(void **delegateOut, void **helperOut) {
    MainHelper *helper = [[MainHelper alloc] init];
    [NSThread detachNewThreadSelector:@selector(doNothing:) toTarget:helper withObject:nil];

    NSApplication *application = [NSApplication sharedApplication];
    id<NSApplicationDelegate> delegate = [[AppDelegate alloc] init];
    [application setDelegate:delegate];
    [application setActivationPolicy:NSApplicationActivationPolicyRegular];
    [application finishLaunching];
    installMainMenu(application);
    [application run];

    *delegateOut = delegate;
    *helperOut = helper;
}

} // namespace

OSX_System::OSX_System()
    : m_isCursorLocked(false), m_staticResourcesPath(findBundleResourcesPath()), m_appDelegate(nullptr),
      m_mainHelper(nullptr), m_sharedGLContext(nullptr) {
    changeToResourcesDirectory(m_staticResourcesPath);
    bootstrapCocoaApplication(&m_appDelegate, &m_mainHelper);

    s_cursors[Cursor::ARROW] = [NSCursor arrowCursor];
    s_cursors[Cursor::IBEAM] = [NSCursor IBeamCursor];
    s_cursors[Cursor::CROSSHAIR] = [NSCursor crosshairCursor];
    s_cursors[Cursor::POINTING_HAND] = [NSCursor pointingHandCursor];
    s_cursors[Cursor::RESIZE_EW] = [NSCursor resizeLeftRightCursor];
    s_cursors[Cursor::RESIZE_NS] = [NSCursor resizeUpDownCursor];
    s_cursors[Cursor::RESIZE_NESW] = nil;
    s_cursors[Cursor::RESIZE_NWSE] = nil;
    s_cursors[Cursor::RESIZE_ALL] = [NSCursor closedHandCursor];
    s_cursors[Cursor::NOT_ALLOWED] = [NSCursor operationNotAllowedCursor];
    
    SEL cursorNWSESelector = NSSelectorFromString(@"_windowResizeNorthWestSouthEastCursor");
    SEL cursorNESWSelector = NSSelectorFromString(@"_windowResizeNorthEastSouthWestCursor");
    if (cursorNWSESelector && [NSCursor respondsToSelector:cursorNWSESelector]) {
        s_cursors[Cursor::RESIZE_NWSE] = [NSCursor performSelector:cursorNWSESelector];
    } else {
        s_cursors[Cursor::RESIZE_NWSE] = [NSCursor crosshairCursor];
    }
    if (cursorNESWSelector && [NSCursor respondsToSelector:cursorNESWSelector]) {
        s_cursors[Cursor::RESIZE_NESW] = [NSCursor performSelector:cursorNESWSelector];
    } else {
        s_cursors[Cursor::RESIZE_NESW] = [NSCursor crosshairCursor];
    }
}

OSX_System::~OSX_System() {
    NSApplication *application = [NSApplication sharedApplication];
    [application setDelegate:nil];
    if (m_appDelegate) {
        [(id)m_appDelegate release];
    }
    if (m_mainHelper) {
        [(id)m_mainHelper release];
    }
}

Window *OSX_System::createWindow(
    const char *title,
    Rect rect,
    WindowState state,
    WindowType windowType,
    WindowFlags flags
) {
    CocoaWindow *window = F_NEW(Foundation::getAllocator(), CocoaWindow)(
        title, rect, state, windowType, flags, static_cast<NSOpenGLContext *>(m_sharedGLContext)
    );
    if (!m_sharedGLContext) { m_sharedGLContext = window->getNativeGLContext(); }
    m_windows.push_back(window);
    return window;
}

void OSX_System::disposeWindow(Window *window) {
    if (window->getState() == WindowState::WindowStateFullScreen) {
        window->setState(WindowState::WindowStateNormal);
    }
    F_DELETE(Foundation::getAllocator(), window);
    m_windows.erase(std::remove(m_windows.begin(), m_windows.end(), window), m_windows.end());
}

void OSX_System::pollEvents() {
    NSApplication* application = [NSApplication sharedApplication];
    while (true) {
        NSEvent *event = [application nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate distantPast]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
        if (!event) { break; }
        [application sendEvent:event];
        [application updateWindows];
    }
    for (CocoaWindow *window : m_windows) {
        window->flushPendingMenuBar();
    }
}

Vec2 OSX_System::getCursorPosition() const {
    const NSPoint mouseLoc = [NSEvent mouseLocation];
    return {static_cast<float>(mouseLoc.x), static_cast<float>(mouseLoc.y)};
}

const char * OSX_System::getClipboardText() {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    if (![[pasteboard types] containsObject:NSPasteboardTypeString]) {
        return nullptr;
    }
    NSString* object = [pasteboard stringForType:NSPasteboardTypeString];
    if (!object) {
        return nullptr;
    }
    return [object UTF8String];
}

void OSX_System::setClipboardText(const char *text) {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard declareTypes:@[NSPasteboardTypeString] owner:nil];
    [pasteboard setString:@(text) forType:NSPasteboardTypeString];
}

void OSX_System::setCursorLocked(bool locked) {
    if (m_isCursorLocked == locked) { return; }
    if (locked) {
        NSWindow* window = [NSApp keyWindow];
        if (!window) { return; }
        m_isCursorLocked = true;
        CGAssociateMouseAndMouseCursorPosition(false);
        CGRect frame = window.frame;
        CGFloat height = window.screen.frame.size.height;
        CGPoint center = CGPointMake(frame.origin.x + frame.size.width / 2,
                                     height - (frame.origin.y + frame.size.height / 2));
        CGWarpMouseCursorPosition(center);
        // Ignore mouse events caused by the programmatic move to avoid spurious deltas
        if ([window.contentView isKindOfClass:[WonderView class]]) {
            WonderView *view = (WonderView *)window.contentView;
            view.ignoreMouseEvents = 3;
        }
        [NSCursor hide];
    } else {
        m_isCursorLocked = false;
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
    }
}

bool OSX_System::isCursorLocked() {
    return m_isCursorLocked;
}

void OSX_System::setCursor(Cursor cursor) {
    if (m_isCursorLocked) { return; }
    NSCursor *nsCursor = s_cursors[cursor];
    if (!nsCursor) { return; }
    [nsCursor set];
}

uint64_t OSX_System::getMilliSeconds() const {
    return (uint64_t)([[NSProcessInfo processInfo] systemUptime] * 1000);
}

const std::filesystem::path& OSX_System::getStaticResourcesPath() {
    return m_staticResourcesPath;
}

std::filesystem::path OSX_System::getPersistentDataPath(const char* appName) {
    NSArray* paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    std::filesystem::path root = [[paths firstObject] UTF8String];
    std::filesystem::path result = root / (appName != nullptr && appName[0] != '\0' ? appName : "PandaGame");
    std::error_code errorCode;
    std::filesystem::create_directories(result, errorCode);
    return result;
}

Foundation::StreamReaderI* OSX_System::createStaticResourceReader(const std::filesystem::path& path) {
    return F_NEW(Foundation::getAllocator(), Foundation::FileStreamReader)(path);
}

void OSX_System::disposeResourceReader(Foundation::StreamReaderI* reader) {
    F_DELETE(Foundation::getAllocator(), reader);
}
    
} // namespace Fern
