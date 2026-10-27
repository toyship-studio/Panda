#pragma once

#include <PandaEditorKit/Document.hpp>

#include <PandaUIGallery/Gallery.hpp>

#include <memory>
#include <string>

namespace Editor {

// The PandaUI gallery in a tab (Help → UI Gallery, the "+" menu): every element of the library with
// a live example under the theme in use — what a theme author looks at while editing the colors of
// a plugin, and the daily check of the editor's PandaUI backend. The gallery is
// PandaUI/examples/gallery; the host (textures, the display face) is the UI host of the application
// layer, behind the bridge. No dirty state: nothing is edited.
class UiGalleryDocument final : public Document {
public:
    explicit UiGalleryDocument(const PandaUI::Gallery::Host &host);

    std::string title() override;
    std::shared_ptr<PandaUI::View> view() override;

private:
    std::shared_ptr<PandaUI::View> m_view;
};

} // namespace Editor
