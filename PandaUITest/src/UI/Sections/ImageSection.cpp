#include "UI/Sections/ImageSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"
#include "UI/Demo/ThemedSurface.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

ImageSection::ImageSection(TextureHandle texture)
    : m_texture(texture) {
    addSubviews();
    setupLayout();
    setupStyle();
}

std::shared_ptr<View> ImageSection::makeModeBox(const std::string &caption, ImageContentMode mode) const {
    auto column = std::make_shared<Panel>();
    column->setBackgroundColor(Color(0x00000000));
    column->layout().setFlexDirection(FlexDirection::Column);
    column->layout().setGap(6.f);
    column->layout().setAlignItems(Align::Center);

    auto box = std::make_shared<ThemedSurface>(SurfaceRole::SurfaceVariant, 8.f);
    box->layout().setSize({96.f, 96.f});
    box->setClipsToBounds(true);

    auto image = std::make_shared<ImageView>(m_texture);
    image->setContentMode(mode);
    image->layout().setWidth(Length::percent(100.f));
    image->layout().setHeight(Length::percent(100.f));
    box->addSubview(image);

    auto label = std::make_shared<Label>(caption);
    label->setFont(Font(Style::BodyFont));
    label->setTextRole(TextRole::Secondary);

    column->addSubview(box);
    column->addSubview(label);
    return column;
}

void ImageSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Image", "ImageView and modes of fitting a texture into a given size."));

    auto example = std::make_shared<DemoExample>("Content modes", "ImageView::setContentMode.");
    example->addContent(makeModeBox("Fit", ImageContentMode::Fit));
    example->addContent(makeModeBox("Fill", ImageContentMode::Fill));
    example->addContent(makeModeBox("Center", ImageContentMode::Center));
    example->addContent(makeModeBox("Stretch", ImageContentMode::Stretch));
    addSubview(example);
}

void ImageSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ImageSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
