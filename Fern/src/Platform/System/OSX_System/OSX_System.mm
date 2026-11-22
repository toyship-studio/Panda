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

void OSX_System::waitEvents(double timeoutSeconds) {
    NSApplication* application = [NSApplication sharedApplication];
    NSEvent *event = [application nextEventMatchingMask:NSEventMaskAny
                                              untilDate:[NSDate dateWithTimeIntervalSinceNow:timeoutSeconds]
                                                 inMode:NSDefaultRunLoopMode
                                                dequeue:YES];
    if (event) {
        [application sendEvent:event];
        [application updateWindows];
    }
    pollEvents(); // the rest of what came, and the menu bars
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

void OSX_System::showAlert(const char *title, const char *message) {
    @autoreleasepool {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setAlertStyle:NSAlertStyleCritical];
        [alert setMessageText:[NSString stringWithUTF8String:title] ?: @""];
        [alert setInformativeText:[NSString stringWithUTF8String:message] ?: @""];
        // The message is why the application is there at all: it comes in front of the others.
        [NSApp activateIgnoringOtherApps:YES];
        [alert runModal];
    }
}

void OSX_System::setApplicationIconLabel(const char *label) {
    @autoreleasepool {
        // The icon of the bundle, taken before the first change: an empty label brings it back.
        static NSImage *s_plainIcon = nil;
        if (s_plainIcon == nil) { s_plainIcon = [[NSApp applicationIconImage] retain]; }
        NSString *text = [NSString stringWithUTF8String:label];
        if (text == nil || [text length] == 0 || s_plainIcon == nil) {
            [NSApp setApplicationIconImage:s_plainIcon];
            return;
        }
        // The hue of the band follows the name (FNV-1a): two projects differ by color even
        // where the tile is too small to read.
        uint32_t hash = 2166136261u;
        for (const char *c = label; *c != 0; ++c) { hash = (hash ^ static_cast<uint8_t>(*c)) * 16777619u; }
        NSColor *bandColor = [NSColor colorWithCalibratedHue:(hash % 360u) / 360.0
                                                  saturation:0.62
                                                  brightness:0.5
                                                       alpha:0.96];
        // Drawn at once into a bitmap: the Dock and the switcher scale it down.
        const NSInteger side = 512;
        NSBitmapImageRep *bitmap = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                            pixelsWide:side
                                                                            pixelsHigh:side
                                                                         bitsPerSample:8
                                                                       samplesPerPixel:4
                                                                              hasAlpha:YES
                                                                              isPlanar:NO
                                                                        colorSpaceName:NSCalibratedRGBColorSpace
                                                                           bytesPerRow:0
                                                                          bitsPerPixel:0] autorelease];
        NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
        if (bitmap == nil || context == nil) { return; }
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        const NSRect bounds = NSMakeRect(0.0, 0.0, side, side);
        [s_plainIcon drawInRect:bounds fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
        // The band lies over the lower part of the icon body (the body leaves a margin of the tile).
        const NSRect band = NSMakeRect(46.0, 52.0, side - 92.0, 132.0);
        [bandColor setFill];
        [[NSBezierPath bezierPathWithRoundedRect:band xRadius:40.0 yRadius:40.0] fill];
        // The largest size at which the name fits; at the floor a longer name ends in an ellipsis.
        const CGFloat available = band.size.width - 40.0;
        NSMutableParagraphStyle *paragraph = [[[NSMutableParagraphStyle alloc] init] autorelease];
        [paragraph setAlignment:NSTextAlignmentCenter];
        [paragraph setLineBreakMode:NSLineBreakByTruncatingTail];
        NSDictionary *attributes = nil;
        for (CGFloat fontSize = 92.0;; fontSize -= 6.0) {
            attributes = @{
                NSFontAttributeName : [NSFont systemFontOfSize:fontSize weight:NSFontWeightSemibold],
                NSForegroundColorAttributeName : [NSColor whiteColor],
                NSParagraphStyleAttributeName : paragraph,
            };
            if (fontSize <= 58.0 || [text sizeWithAttributes:attributes].width <= available) { break; }
        }
        const CGFloat textHeight = [text sizeWithAttributes:attributes].height;
        [text drawInRect:NSMakeRect(
                             band.origin.x + 20.0, band.origin.y + (band.size.height - textHeight) * 0.5, available,
                             textHeight
                         )
            withAttributes:attributes];
        [NSGraphicsContext restoreGraphicsState];
        NSImage *image = [[[NSImage alloc] initWithSize:NSMakeSize(side, side)] autorelease];
        [image addRepresentation:bitmap];
        [NSApp setApplicationIconImage:image];
    }
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
    return root / (appName != nullptr && appName[0] != '\0' ? appName : "PandaGame");
}

Foundation::StreamReaderI* OSX_System::createStaticResourceReader(const std::filesystem::path& path) {
    return F_NEW(Foundation::getAllocator(), Foundation::FileStreamReader)(path);
}

void OSX_System::disposeResourceReader(Foundation::StreamReaderI* reader) {
    F_DELETE(Foundation::getAllocator(), reader);
}
    
} // namespace Fern
