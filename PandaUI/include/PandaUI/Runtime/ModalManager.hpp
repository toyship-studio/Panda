#pragma once

#include <memory>
#include <vector>

namespace PandaUI {

class Context;
class Modal;

// The modal stack of a single context (Context::modals()) — the only point for showing and dismissing.
// It owns the open modals: the caller does not need to keep a pointer and remove the overlay
// manually. The top modal receives the input; dismissing a lower one also closes everything above it.
class ModalManager final {
public:
    explicit ModalManager(Context &context);

    void present(std::shared_ptr<Modal> modal);
    void dismiss(Modal *modal);
    void dismissAll();

    bool isPresenting() const {
        return !m_stack.empty();
    }
    Modal *top() const {
        return m_stack.empty() ? nullptr : m_stack.back().get();
    }

private:
    friend class Context;
    void clear(); // Context::setRootView: the overlays are already removed, dismiss callbacks are not called

    Context &m_context;
    std::vector<std::shared_ptr<Modal>> m_stack;
};

} // namespace PandaUI
