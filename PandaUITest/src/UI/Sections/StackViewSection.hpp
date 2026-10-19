#pragma once

#include "UI/Demo/DemoComponent.hpp"

namespace Demo {

// Stack View section: syntactic sugar over the flex layout — vertical and horizontal stacks.
class StackViewSection final : public DemoComponent {
public:
    StackViewSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace Demo
