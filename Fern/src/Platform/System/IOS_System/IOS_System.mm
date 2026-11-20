#import "IOS_System.hpp"
#import "UIKitWindow/UIKitWindow.h"
#import "UIKitWindow/WonderView.h"
#import "UIKitWindow/WonderController.h"

#import <Foundation/IO/FileStreamReader.hpp>
#import <UIKit/UIKit.h>

#include <algorithm>

extern std::filesystem::path g_ios_resources_path;

namespace Fern {

IOS_System::IOS_System() {}

Window *IOS_System::createWindow(
    const char *title,
    Rect rect,
    WindowState state,
    WindowType windowType,
    WindowFlags flags
) {
    return F_NEW(Foundation::getAllocator(), UIKitWindow)();
}

void IOS_System::disposeWindow(Window *window) {
    F_DELETE(Foundation::getAllocator(), window);
}

void IOS_System::pollEvents() {}

Vec2 IOS_System::getCursorPosition() const {
    return Vec2(); // fingers, no cursor
}

const char* IOS_System::getClipboardText() {
    return ""; // not read from the system yet
}

void IOS_System::setClipboardText(const char *text) {}

void IOS_System::setCursorLocked(bool locked) {}

bool IOS_System::isCursorLocked() {
    return false;
}

void IOS_System::setCursor(Cursor cursor) {}

void IOS_System::playHaptic(Haptic haptic) {
    // A generator per strength, kept and prepared again after each tap: a prepared one taps
    // at once. They live as long as the application.
    static UIImpactFeedbackGenerator *generators[3] = {nil, nil, nil};
    const int index = std::clamp(static_cast<int>(haptic), 0, 2);
    void (^tap)(void) = ^{
        if (generators[index] == nil) {
            const UIImpactFeedbackStyle styles[3] = {
                UIImpactFeedbackStyleLight, UIImpactFeedbackStyleMedium, UIImpactFeedbackStyleHeavy};
            generators[index] = [[UIImpactFeedbackGenerator alloc] initWithStyle:styles[index]];
        }
        [generators[index] impactOccurred];
        [generators[index] prepare];
    };
    // UIKit belongs to the main thread.
    if ([NSThread isMainThread]) {
        tap();
    } else {
        dispatch_async(dispatch_get_main_queue(), tap);
    }
}

uint64_t IOS_System::getMilliSeconds() const {
    return (uint64_t)([[NSProcessInfo processInfo] systemUptime] * 1000);
}

const std::filesystem::path& IOS_System::getStaticResourcesPath() {
    return g_ios_resources_path;
}

std::filesystem::path IOS_System::getPersistentDataPath(const char*) {
    NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    return std::filesystem::path([[paths firstObject] UTF8String]); // Documents: the system made it
}

Foundation::StreamReaderI* IOS_System::createStaticResourceReader(
    const std::filesystem::path &path
) {
    return F_NEW(Foundation::getAllocator(), Foundation::FileStreamReader)(path);
}

void IOS_System::disposeResourceReader(Foundation::StreamReaderI *reader) {
    F_DELETE(Foundation::getAllocator(), reader);
}

} // namespace Fern
