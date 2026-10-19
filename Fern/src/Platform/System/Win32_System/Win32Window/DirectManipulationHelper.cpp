#include "DirectManipulationHelper.hpp"
#include "Win32_Window.hpp"

#include <Foundation/Foundation.hpp>

namespace Fern {

// Receives viewport callbacks during DirectManipulationHelper::update(). The
// content transform accumulates the gesture translation; deltas against the
// cached values become Fern scroll events with proper phase flags.
class DManipEventHandler final : public IDirectManipulationViewportEventHandler {
public:
    explicit DManipEventHandler(Win32_Window *window)
        : m_refCount(1)
        , m_window(window) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
        if (!object) { return E_POINTER; }
        if (riid == IID_IUnknown || riid == __uuidof(IDirectManipulationViewportEventHandler)) {
            *object = static_cast<IDirectManipulationViewportEventHandler *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) { delete this; }
        return static_cast<ULONG>(count);
    }

    // IDirectManipulationViewportEventHandler
    HRESULT STDMETHODCALLTYPE OnViewportStatusChanged(
        IDirectManipulationViewport *viewport, DIRECTMANIPULATION_STATUS current, DIRECTMANIPULATION_STATUS previous
    ) override {
        if (current == previous) { return S_OK; }
        if (current == DIRECTMANIPULATION_RUNNING) {
            // A fresh gesture right after a reset can interleave with the reset's
            // async content update; baseline the first update instead of posting
            // a huge stale-transform delta (content visibly jumps otherwise).
            if (m_state == State::None) { m_syncNextContentUpdate = true; }
            m_state = State::Running;
        } else if (current == DIRECTMANIPULATION_INERTIA) {
            // Fingers lifted: Cocoa sends phase=Ended before momentum events start.
            if (m_state == State::Running) { m_window->postDirectManipulationGestureEnd(); }
            m_state = State::Inertia;
        } else if (current == DIRECTMANIPULATION_READY) {
            if (m_state == State::Running) { m_window->postDirectManipulationGestureEnd(); }
            m_state = State::None;
            // Reset the accumulated transform so translation never overflows.
            // OnContentUpdated syncs the cached values while the state is None.
            m_syncNextContentUpdate = true;
            viewport->ZoomToRect(0.f, 0.f, 1000.f, 1000.f, FALSE);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnViewportUpdated(IDirectManipulationViewport *) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnContentUpdated(
        IDirectManipulationViewport *, IDirectManipulationContent *content
    ) override {
        float transform[6];
        if (FAILED(content->GetContentTransform(transform, 6))) { return S_OK; }
        const float x = transform[4];
        const float y = transform[5];
        if (m_state == State::None || m_syncNextContentUpdate) {
            m_syncNextContentUpdate = false;
            m_lastX = x;
            m_lastY = y;
            return S_OK;
        }
        const float dx = x - m_lastX;
        const float dy = y - m_lastY;
        m_lastX = x;
        m_lastY = y;
        if (dx == 0.f && dy == 0.f) { return S_OK; }
        m_window->postDirectManipulationScroll(dx, dy, m_state == State::Inertia);
        return S_OK;
    }

private:
    enum class State { None, Running, Inertia };

    volatile LONG m_refCount;
    Win32_Window *m_window;
    State m_state = State::None;
    bool m_syncNextContentUpdate = false;
    float m_lastX = 0.f;
    float m_lastY = 0.f;
};

DirectManipulationHelper *DirectManipulationHelper::create(Win32_Window *window, HWND hwnd) {
    if (!window || !hwnd) { return nullptr; }
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) { return nullptr; }

    IDirectManipulationManager *manager = nullptr;
    IDirectManipulationUpdateManager *updateManager = nullptr;
    IDirectManipulationViewport *viewport = nullptr;
    DManipEventHandler *handler = nullptr;
    DWORD cookie = 0;

    auto fail = [&]() -> DirectManipulationHelper * {
        if (handler) { handler->Release(); }
        if (viewport) { viewport->Release(); }
        if (updateManager) { updateManager->Release(); }
        if (manager) { manager->Release(); }
        return nullptr;
    };

    if (FAILED(CoCreateInstance(
            CLSID_DirectManipulationManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager)
        ))) {
        return fail();
    }
    if (FAILED(manager->GetUpdateManager(IID_PPV_ARGS(&updateManager)))) { return fail(); }
    if (FAILED(manager->CreateViewport(nullptr, hwnd, IID_PPV_ARGS(&viewport)))) { return fail(); }

    const DIRECTMANIPULATION_CONFIGURATION configuration =
        DIRECTMANIPULATION_CONFIGURATION_INTERACTION | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X |
        DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA;
    if (FAILED(viewport->ActivateConfiguration(configuration))) { return fail(); }
    if (FAILED(viewport->SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE))) { return fail(); }

    handler = new DManipEventHandler(window);
    if (FAILED(viewport->AddEventHandler(hwnd, handler, &cookie))) { return fail(); }

    RECT viewportRect = {0, 0, 1000, 1000};
    if (FAILED(viewport->SetViewportRect(&viewportRect))) { return fail(); }
    if (FAILED(manager->Activate(hwnd))) { return fail(); }
    if (FAILED(viewport->Enable())) { return fail(); }
    updateManager->Update(nullptr);

    auto *helper = F_NEW(Foundation::getAllocator(), DirectManipulationHelper);
    helper->m_hwnd = hwnd;
    helper->m_manager = manager;
    helper->m_updateManager = updateManager;
    helper->m_viewport = viewport;
    helper->m_handler = handler;
    helper->m_handlerCookie = cookie;
    return helper;
}

DirectManipulationHelper::~DirectManipulationHelper() {
    if (m_viewport) {
        m_viewport->Stop();
        if (m_handlerCookie) { m_viewport->RemoveEventHandler(m_handlerCookie); }
        m_viewport->Abandon();
        m_viewport->Release();
    }
    if (m_manager) {
        m_manager->Deactivate(m_hwnd);
        m_manager->Release();
    }
    if (m_updateManager) { m_updateManager->Release(); }
    if (m_handler) { m_handler->Release(); }
}

void DirectManipulationHelper::update() {
    if (m_updateManager) { m_updateManager->Update(nullptr); }
}

bool DirectManipulationHelper::onPointerHitTest(WPARAM wParam) {
    if (!m_viewport) { return false; }
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INPUT_TYPE pointerType;
    if (!GetPointerType(pointerId, &pointerType) || pointerType != PT_TOUCHPAD) { return false; }
    return SUCCEEDED(m_viewport->SetContact(pointerId));
}

} // namespace Fern
