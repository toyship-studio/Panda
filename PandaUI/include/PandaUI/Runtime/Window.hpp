#pragma once

#include "PandaUI/Runtime/Context.hpp"
#include "PandaUI/Core/Event.hpp"

#include <memory>
#include <utility>

namespace PandaUI {

struct WindowCreateInfo {
    const char *title = "PandaUI Window";
    float x = 80.f;
    float y = 80.f;
    float width = 720.f;
    float height = 480.f;
};

class WindowHost {
public:
    virtual ~WindowHost() = default;

    virtual WindowHandle getMainWindow() = 0;
    virtual WindowHandle createWindow(const WindowCreateInfo &info) = 0;
    virtual void destroyWindow(WindowHandle handle) = 0;
    virtual bool isWindowValid(WindowHandle handle) = 0;
    virtual Context *getWindowContext(WindowHandle handle) = 0;
};

void setWindowHost(WindowHost *host);
void clearWindowHost();

class Window final {
public:
    Window() = default;
    explicit Window(WindowHandle handle);

    static Window main();
    static Window create(WindowCreateInfo info = {});

    bool isValid() const;
    WindowHandle getHandle() const;

    const Context &context() const;
    Context &context();

    template<typename T, typename... Args>
    std::shared_ptr<T> makeView(Args &&...args) {
        return context().makeView<T>(std::forward<Args>(args)...);
    }

    void setRootView(std::shared_ptr<View> view);
    void setSafeAreaInsets(EdgeInsets insets);
    EdgeInsets getSafeAreaInsets() const;
    void close();
    bool update(double deltaTime);
    bool handleEvent(const Event &event);
    const DrawList &getDrawList() const;

private:
    Context *getContext() const;

    WindowHandle m_handle = 0;
};

} // namespace PandaUI
