#pragma once

#include "UI/Demo/DemoComponent.hpp"
#include "UI/Demo/SkinAssets.hpp"

#include <memory>

namespace Demo {

// Skins: the same controls twice — the editor look (the default appearances) and a "Fantasy" skin
// built from nine-slice plates, a display font, text decorations and gradients through the
// appearance of every widget (Appearance.hpp). If the right column reads as another game, the
// customization works.
class SkinsSection final : public DemoComponent {
public:
    explicit SkinsSection(SkinAssets assets);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::View> makeEditorColumn() const;
    std::shared_ptr<PandaUI::View> makeFantasyColumn() const;

    SkinAssets m_assets;
};

} // namespace Demo
