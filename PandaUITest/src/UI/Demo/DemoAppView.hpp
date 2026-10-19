#pragma once

#include "UI/Demo/DemoComponent.hpp"
#include "UI/Demo/DemoSidebar.hpp"

#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Demo {

// The root view of the demo: a scrollable sidebar with the list of sections on the left, the
// scrollable content of the selected section on the right. Sections are created on demand (factories).
class DemoAppView final : public DemoComponent {
public:
    using OpenWindowCallback = std::function<void()>;

    DemoAppView(PandaUI::TextureHandle logoTexture, OpenWindowCallback onOpenWindow);

private:
    struct SectionEntry {
        std::string title;
        std::function<std::shared_ptr<PandaUI::View>()> make;
    };

    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
    void buildSections();
    void selectSection(int index);

    PandaUI::TextureHandle m_logoTexture;
    OpenWindowCallback m_onOpenWindow;
    std::vector<SectionEntry> m_sections;
    std::shared_ptr<DemoSidebar> m_sidebar;
    std::shared_ptr<PandaUI::ScrollView> m_content;
};

} // namespace Demo
