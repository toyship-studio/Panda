#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace PandaUI {

// Platform clipboard. PandaUI does not depend on the platform, so the host (bridge)
// provides the implementation through these hooks, and controls access the clipboard through
// getClipboardText/setClipboardText. If the hooks are not set, the operations are no-ops.
struct ClipboardHooks {
    std::function<std::string()> getText;
    std::function<void(std::string_view)> setText;
};

void setClipboardHooks(ClipboardHooks hooks);
std::string getClipboardText();
void setClipboardText(std::string_view text);

} // namespace PandaUI
