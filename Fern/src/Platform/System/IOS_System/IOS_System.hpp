#pragma once

#include "Fern/Platform/PlatformSystem.hpp"

namespace Fern {

class IOS_System final : public PlatformSystem {
public:
    IOS_System();
    Window *createWindow(
        const char *title,
        Rect rect,
        WindowState state,
        WindowType windowType,
        WindowFlags flags
    ) override;
    void disposeWindow(Window *window) override;
    void pollEvents() override;
    Vec2 getCursorPosition() const override;
    const char *getClipboardText() override;
    void setClipboardText(const char *text) override;
    void setCursorLocked(bool locked) override;
    bool isCursorLocked() override;
    void setCursor(Cursor cursor) override;
    uint64_t getMilliSeconds() const override;
    const std::filesystem::path &getStaticResourcesPath() override;
    std::filesystem::path getPersistentDataPath(const char *appName) override;
    Foundation::StreamReaderI *createStaticResourceReader(const std::filesystem::path &path) override;
    void disposeResourceReader(Foundation::StreamReaderI *reader) override;
};

} // namespace Fern
