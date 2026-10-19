#include "PandaUI/Runtime/Window.hpp"

#include <cassert>
#include <utility>

namespace PandaUI {

namespace {

    WindowHost *s_host = nullptr;

} // namespace

void setWindowHost(WindowHost *host) {
    s_host = host;
}

void clearWindowHost() {
    s_host = nullptr;
}

Window::Window(WindowHandle handle)
    : m_handle(handle) {}

Window Window::main() {
    if (!s_host) { return {}; }
    return Window(s_host->getMainWindow());
}

Window Window::create(WindowCreateInfo info) {
    if (!s_host) { return {}; }
    return Window(s_host->createWindow(info));
}

bool Window::isValid() const {
    if (m_handle == 0) { return false; }
    return s_host && s_host->isWindowValid(m_handle);
}

WindowHandle Window::getHandle() const {
    return m_handle;
}

Context *Window::getContext() const {
    if (!isValid()) { return nullptr; }
    return s_host->getWindowContext(m_handle);
}

Context &Window::context() {
    Context *result = getContext();
    assert(result != nullptr && "PandaUI window context is unavailable");
    return *result;
}

const Context &Window::context() const {
    Context *result = getContext();
    assert(result != nullptr && "PandaUI window context is unavailable");
    return *result;
}

void Window::setRootView(std::shared_ptr<View> view) {
    context().setRootView(std::move(view));
}

void Window::setSafeAreaInsets(EdgeInsets insets) {
    context().setSafeAreaInsets(insets);
}

EdgeInsets Window::getSafeAreaInsets() const {
    return context().getSafeAreaInsets();
}

void Window::close() {
    if (!m_handle || !s_host) { return; }
    s_host->destroyWindow(m_handle);
    m_handle = 0;
}

bool Window::update(double deltaTime) {
    if (Context *windowContext = getContext()) { return windowContext->update(deltaTime); }
    return false;
}

bool Window::handleEvent(const Event &event) {
    if (!isValid() || event.window != m_handle) { return false; }
    Context *windowContext = getContext();
    if (!windowContext) { return false; }

    switch (event.type) {
        case EventType::WindowResize:
            windowContext->setSafeAreaInsets(event.safeAreaInsets);
            windowContext->updateViewportSize(event.windowSize, event.dpiScale);
            return false;
        case EventType::PointerDown:
            return windowContext->handlePointerDown(event.pointer);
        case EventType::PointerUp:
            return windowContext->handlePointerUp(event.pointer);
        case EventType::PointerMove:
            return windowContext->handlePointerMove(event.pointer);
        case EventType::Scroll:
            return windowContext->handleScroll(event.scroll);
        case EventType::KeyDown:
            return windowContext->handleKeyDown(event.key);
        case EventType::KeyUp:
            return windowContext->handleKeyUp(event.key);
        case EventType::TextInput:
            return windowContext->handleTextInput(event.textInput);
        default:
            return false;
    }
}

const DrawList &Window::getDrawList() const {
    return context().getDrawList();
}

} // namespace PandaUI
