#include "UI/Sections/SurfaceSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

SurfaceSection::SurfaceSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

std::shared_ptr<Panel> SurfaceSection::makeTile() const {
    auto tile = std::make_shared<Panel>();
    tile->layout().setSize({76.f, 76.f});
    tile->setBackgroundColor(theme().accent);
    return tile;
}

void SurfaceSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Surface", "A view's visual layer: corner radius, border, shadow, opacity."));

    {
        auto example = std::make_shared<DemoExample>("Corner radius", "Surface::setCornerRadius.");
        auto a = makeTile();
        a->surface().setCornerRadius(0.f);
        auto b = makeTile();
        b->surface().setCornerRadius(12.f);
        auto c = makeTile();
        c->surface().setCornerRadius(38.f);
        example->addContent(a);
        example->addContent(b);
        example->addContent(c);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Border", "Surface::setBorderWidth / setBorderColor.");
        auto tile = makeTile();
        tile->setBackgroundColor(theme().surface);
        tile->surface().setCornerRadius(10.f);
        tile->surface().setBorderWidth(2.f);
        tile->surface().setBorderColor(theme().accent);
        example->addContent(tile);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Shadow", "Surface::setShadow* (soft shadow).");
        auto tile = makeTile();
        tile->surface().setCornerRadius(12.f);
        tile->surface().setShadowColor(Color(0x00000077));
        tile->surface().setShadowOffset(Point(0.f, 6.f));
        tile->surface().setShadowRadius(16.f);
        example->addContent(tile);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Opacity", "View::setOpacity.");
        auto opaque = makeTile();
        opaque->surface().setCornerRadius(10.f);
        auto translucent = makeTile();
        translucent->surface().setCornerRadius(10.f);
        translucent->setOpacity(0.4f);
        example->addContent(opaque);
        example->addContent(translucent);
        addSubview(example);
    }
}

void SurfaceSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void SurfaceSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
