#include <PandaUIGallery/Gallery.hpp>

#include "Demo/GalleryView.hpp"

#include <memory>

namespace PandaUI::Gallery {

std::shared_ptr<View> makeGallery(const Host &host) {
    return std::make_shared<GalleryView>(host);
}

} // namespace PandaUI::Gallery
