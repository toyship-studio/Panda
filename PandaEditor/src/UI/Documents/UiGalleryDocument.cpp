#include "UI/Documents/UiGalleryDocument.hpp"

namespace Editor {

UiGalleryDocument::UiGalleryDocument(const PandaUI::Gallery::Host &host)
    : m_view(PandaUI::Gallery::makeGallery(host)) {}

std::string UiGalleryDocument::title() {
    return "UI Gallery";
}

std::shared_ptr<PandaUI::View> UiGalleryDocument::view() {
    return m_view;
}

} // namespace Editor
