#pragma once

#include <Panda/Base/Base.hpp>
#include <Panda/ScriptEngine/ABI/ScriptApi.hpp>

namespace Fern {
struct Event;
class Window;
} // namespace Fern

namespace Panda {

class PandaUIImpl final {
public:
    explicit PandaUIImpl(Fern::Window *mainWindow);
    ~PandaUIImpl() = default;

    void initialize();
    void shutdown();
    void update(float deltaTime);
    void syncWindow(Fern::Window *window);
    bool handleEvent(Fern::Event *event);
    void setMainViewportFrame(Rect frame, Size dpiScale);
    void clearMainViewportFrame();

    static void bindHostApi(ScriptHostApi &hostApi);
    static void flushPendingWindowDisposals();

private:
    Fern::Window *m_window;
    bool m_initialized = false;
};

} // namespace Panda
