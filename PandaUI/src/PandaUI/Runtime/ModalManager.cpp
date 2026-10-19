#include "PandaUI/Runtime/ModalManager.hpp"

#include "PandaUI/Elements/Modal.hpp"
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <utility>

namespace PandaUI {

ModalManager::ModalManager(Context &context)
    : m_context(context) {}

void ModalManager::present(std::shared_ptr<Modal> modal) {
    if (!modal) { return; }
    m_stack.push_back(modal);
    m_context.addOverlay(std::move(modal));
}

void ModalManager::dismiss(Modal *modal) {
    const auto it = std::find_if(m_stack.begin(), m_stack.end(), [modal](const std::shared_ptr<Modal> &entry) {
        return entry.get() == modal;
    });
    if (it == m_stack.end()) { return; }

    // Popups opened from a modal (a combobox list, a color picker) lie above it: without the anchor
    // they have no reason to live. Dismissing a popup calls the owner's callback — the owner learns
    // about the closing.
    for (View *overlay : m_context.overlaysAbove(modal)) {
        if (auto *popup = dynamic_cast<Popup *>(overlay)) { popup->dismiss(); }
    }

    // Dismissing a lower modal also closes everything above it. The stack is updated BEFORE the callbacks:
    // onDismiss is allowed to show the next modal right away.
    std::vector<std::shared_ptr<Modal>> closing(it, m_stack.end());
    m_stack.erase(it, m_stack.end());
    for (auto entry = closing.rbegin(); entry != closing.rend(); ++entry) {
        m_context.requestRemoveOverlay(entry->get());
        (*entry)->notifyDismissed();
    }
}

void ModalManager::dismissAll() {
    if (!m_stack.empty()) { dismiss(m_stack.front().get()); }
}

void ModalManager::clear() {
    m_stack.clear();
}

} // namespace PandaUI
