#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <functional>

namespace Demo {

// Windows section: several windows, each with its own Context.
class WindowsSection final : public DemoComponent {
public:
    using OpenWindowCallback = std::function<void()>;
    explicit WindowsSection(OpenWindowCallback onOpenWindow);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    OpenWindowCallback m_onOpenWindow;
};

} // namespace Demo
