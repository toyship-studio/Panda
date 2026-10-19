#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>

namespace Demo {

// Modal section: blocking overlays through Context::modals() — Dialog (a card with
// a title/body/buttons) and an arbitrary Modal that closes on an outside click.
class ModalSection final : public DemoComponent {
public:
    ModalSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_resultLabel;
};

} // namespace Demo
