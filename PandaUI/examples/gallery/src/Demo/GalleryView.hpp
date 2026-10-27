#pragma once

#include "Demo/DemoComponent.hpp"
#include "Demo/DemoSidebar.hpp"
#include "Demo/SkinAssets.hpp"

#include <PandaUIGallery/Gallery.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PandaUI::Gallery {

// The root view of the gallery: a scrollable sidebar with the list of sections on the left, the
// scrollable content of the selected section on the right. Sections are created on demand
// (factories); the ones that need a capability the host did not give are not listed.
class GalleryView final : public DemoComponent {
public:
    explicit GalleryView(Host host);
    ~GalleryView() override;

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

    Host m_host;
    SkinAssets m_skinAssets; // the plates of the "Fantasy" skin, drawn at start; released with the view
    std::vector<SectionEntry> m_sections;
    std::shared_ptr<DemoSidebar> m_sidebar;
    std::shared_ptr<PandaUI::ScrollView> m_content;
};

} // namespace PandaUI::Gallery
