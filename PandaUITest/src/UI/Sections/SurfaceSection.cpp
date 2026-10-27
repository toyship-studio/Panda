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
    tile->setBackgroundColor(ThemeColor::accent);
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
        tile->setBackgroundColor(ThemeColor::surface);
        tile->surface().setCornerRadius(10.f);
        tile->surface().setBorderWidth(2.f);
        tile->surface().setBorderColor(ThemeColor::accent);
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
        auto example = std::make_shared<DemoExample>(
            "Per-corner radii", "Surface::setCornerRadii: CornerRadii::top / left, or four values (the tessellated path)."
        );
        auto top = makeTile();
        top->surface().setCornerRadii(CornerRadii::top(16.f));
        auto left = makeTile();
        left->surface().setCornerRadii(CornerRadii::left(38.f));
        auto mixed = makeTile();
        mixed->surface().setCornerRadii(CornerRadii(24.f, 4.f, 24.f, 4.f));
        mixed->surface().setBorderWidth(2.f);
        mixed->surface().setBorderColor(ThemeColor::textPrimary);
        example->addContent(top);
        example->addContent(left);
        example->addContent(mixed);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>(
            "Gradient", "Surface::setBackgroundGradient: two colors along a direction, with the corners."
        );
        auto vertical = makeTile();
        vertical->surface().setCornerRadius(12.f);
        vertical->surface().setBackgroundGradient(SurfaceGradient(ThemeColor::accent, ThemeColor::accentPressed));
        auto horizontal = makeTile();
        horizontal->surface().setCornerRadius(12.f);
        horizontal->surface().setBackgroundGradient(
            SurfaceGradient(ThemeColor::accentSecondary, ThemeColor::accent, GradientDirection::LeftToRight)
        );
        auto diagonal = makeTile();
        diagonal->surface().setCornerRadii(CornerRadii(24.f, 4.f, 24.f, 4.f));
        diagonal->surface().setBackgroundGradient(
            SurfaceGradient(ThemeColor::warning, ThemeColor::danger, GradientDirection::TopLeftToBottomRight)
        );
        example->addContent(vertical);
        example->addContent(horizontal);
        example->addContent(diagonal);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>(
            "Rounded clip", "clipsToBounds with a corner radius clips the subviews by the rounded shape (masksToBounds)."
        );
        auto clipped = makeTile();
        clipped->surface().setCornerRadius(24.f);
        clipped->setClipsToBounds(true);
        auto stripe = std::make_shared<Panel>();
        stripe->setBackgroundColor(ThemeColor::textPrimary);
        stripe->layout().setPositionType(PositionType::Absolute);
        stripe->layout().setPosition(Edge::Left, Length::points(-10.f));
        stripe->layout().setPosition(Edge::Top, Length::points(0.f));
        stripe->layout().setSize({96.f, 18.f});
        clipped->addSubview(stripe);
        auto unclipped = makeTile();
        unclipped->surface().setCornerRadius(24.f);
        auto stripe2 = std::make_shared<Panel>();
        stripe2->setBackgroundColor(ThemeColor::textPrimary);
        stripe2->layout().setPositionType(PositionType::Absolute);
        stripe2->layout().setPosition(Edge::Left, Length::points(-10.f));
        stripe2->layout().setPosition(Edge::Top, Length::points(0.f));
        stripe2->layout().setSize({96.f, 18.f});
        unclipped->addSubview(stripe2);
        example->addContent(clipped);
        example->addContent(unclipped);
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
