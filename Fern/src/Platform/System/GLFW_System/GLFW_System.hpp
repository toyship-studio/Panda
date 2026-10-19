#include "Fern/Platform/PlatformSystem.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>
#include <GLFW/glfw3.h>

namespace Fern {

class GLFW_Window;

class GLFW_System final : public PlatformSystem {
public:
    GLFW_System();
    ~GLFW_System() override;
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

private:
    GLFW_Window *getFocusedWindow() const;
    void resetCursorPos();

    std::vector<GLFW_Window *> m_windows;
    bool m_isCursorLocked;
    std::filesystem::path m_staticResourcesPath;
    GLFWcursor *m_cursors[Cursor::COUNT];
};

} // namespace Fern
