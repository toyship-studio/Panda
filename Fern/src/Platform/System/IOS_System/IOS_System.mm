#import "IOS_System.hpp"
#import "UIKitWindow/UIKitWindow.h"
#import "UIKitWindow/WonderView.h"
#import "UIKitWindow/WonderController.h"

#import <Foundation/IO/FileStreamReader.hpp>

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

Vec2 IOS_System::getCursorPosition() const {}

const char* IOS_System::getClipboardText() {}

void IOS_System::setClipboardText(const char *text) {}

void IOS_System::setCursorLocked(bool locked) {}

bool IOS_System::isCursorLocked() {
    return false;
}

void IOS_System::setCursor(Cursor cursor) {}

uint64_t IOS_System::getMilliSeconds() const {
    return (uint64_t)([[NSProcessInfo processInfo] systemUptime] * 1000);
}

const std::filesystem::path& IOS_System::getStaticResourcesPath() {
    return g_ios_resources_path;
}

std::filesystem::path IOS_System::getPersistentDataPath(const char*) {
    NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    std::filesystem::path result = [[paths firstObject] UTF8String];
    std::error_code errorCode;
    std::filesystem::create_directories(result, errorCode);
    return result;
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
