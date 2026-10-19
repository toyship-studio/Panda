#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>

namespace Demo {

// TextField section: single/multiline, placeholder, live input, selection, clipboard.
class TextFieldSection final : public DemoComponent {
public:
    TextFieldSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_echoLabel;
};

} // namespace Demo
