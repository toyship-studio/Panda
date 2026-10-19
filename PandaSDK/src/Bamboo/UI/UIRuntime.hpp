#pragma once

#include <PandaUI/Core/Event.hpp>

namespace Bamboo::UIRuntime {

// Entry points of the SDK's UI runtime. Their addresses are passed to the engine through the ABI
// (ScriptPluginApi), so they remain free functions. Internally they delegate to UI::Platform /
// UI::WindowRegistry.
void initialize();
void shutdown();
void reset();

namespace Internal {
    void update(float deltaTime);
    bool handleEvent(const PandaUI::Event *event);
    void windowClosed(PandaUI::WindowHandle windowHandle);
} // namespace Internal

} // namespace Bamboo::UIRuntime
