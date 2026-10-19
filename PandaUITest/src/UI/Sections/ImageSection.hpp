#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <string>

namespace Demo {

// ImageView section: texture fitting modes (content modes).
class ImageSection final : public DemoComponent {
public:
    explicit ImageSection(PandaUI::TextureHandle texture);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::View> makeModeBox(const std::string &caption, PandaUI::ImageContentMode mode) const;

    PandaUI::TextureHandle m_texture;
};

} // namespace Demo
