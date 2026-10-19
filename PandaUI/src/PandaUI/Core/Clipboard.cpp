#include "PandaUI/Core/Clipboard.hpp"

#include <utility>

namespace PandaUI {

namespace {

    ClipboardHooks s_hooks;

} // namespace

void setClipboardHooks(ClipboardHooks hooks) {
    s_hooks = std::move(hooks);
}

std::string getClipboardText() {
    return s_hooks.getText ? s_hooks.getText() : std::string();
}

void setClipboardText(std::string_view text) {
    if (s_hooks.setText) { s_hooks.setText(text); }
}

} // namespace PandaUI
