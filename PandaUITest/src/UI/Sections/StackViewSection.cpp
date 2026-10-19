#include "UI/Sections/StackViewSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <memory>
#include <string>

namespace Demo {

using namespace PandaUI;

namespace {

    // A colored "chip" with a caption — a visual element for demonstrating stacks.
    std::shared_ptr<View> makeChip(const std::string &text) {
        auto chip = std::make_shared<Panel>();
        chip->setBackgroundColor(theme().control);
        chip->surface().setCornerRadius(6.f);
        chip->layout().setPadding(Edge::Horizontal, 14.f);
        chip->layout().setPadding(Edge::Vertical, 9.f);

        auto label = std::make_shared<Label>(text);
        label->setFont(Font(17.f));
        chip->addSubview(label);
        return chip;
    }

} // namespace

StackViewSection::StackViewSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void StackViewSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Stack View", "Syntactic sugar over flex: StackView with a direction (VStack/HStack), spacing and alignment."
    ));

    // Vertical stack.
    {
        auto example = std::make_shared<DemoExample>("Vertical (VStack)", "StackView::vertical(8) — children in a column with gaps.");
        auto stack = StackView::vertical(8.f);
        stack->setAlignment(Align::Start);
        stack->addSubview(makeChip("First"));
        stack->addSubview(makeChip("Second"));
        stack->addSubview(makeChip("Third"));
        example->addContent(stack);
        addSubview(example);
    }

    // Horizontal stack.
    {
        auto example =
            std::make_shared<DemoExample>("Horizontal (HStack)", "StackView::horizontal(8), center alignment.");
        auto stack = StackView::horizontal(8.f);
        stack->setAlignment(Align::Center);
        stack->addSubview(makeChip("One"));
        stack->addSubview(makeChip("Two"));
        stack->addSubview(makeChip("Three"));
        stack->addSubview(makeChip("Four"));
        example->addContent(stack);
        addSubview(example);
    }
}

void StackViewSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void StackViewSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
