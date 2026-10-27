#include "Sections/DragDropSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace PandaUI::Gallery {

namespace {

    class DropZone;
    constexpr const char *CHIP_TYPE = "demo.chip";

    // A draggable "chip". The Control captures pointerDown → becomes the drag source.
    class DragChip final : public Control {
    public:
        DragChip(std::string label, DropZone *zone)
            : m_label(std::move(label))
            , m_zone(zone) {
            setCursor(Cursor::PointingHand);
            layout().setPadding(Edge::Horizontal, 12.f);
            layout().setPadding(Edge::Vertical, 7.f);
            surface().setCornerRadius(6.f);
            setBackgroundColor(ThemeColor::control);

            auto labelView = std::make_shared<Label>(m_label);
            labelView->setFont(Font(17.f));
            addSubview(labelView);
        }

        bool prepareDrag(DragPayload &payload, Point) override {
            payload.type = CHIP_TYPE;
            payload.source = m_zone;     // source zone (identity + removeChip on transfer)
            payload.ghostText = m_label; // chip label = the data being transferred
            return true;
        }

    private:
        std::string m_label;
        DropZone *m_zone;
    };

    // Receiving zone: a vertical list of chips, accepts a drop of type CHIP_TYPE.
    class DropZone final : public Panel {
    public:
        DropZone(std::string title, std::vector<std::string> chips)
            : m_chips(std::move(chips)) {
            layout().setFlexDirection(FlexDirection::Column);
            layout().setGap(8.f);
            layout().setPadding(12.f);
            layout().setFlexGrow(1.f);
            layout().setFlexShrink(1.f);
            layout().setMinHeight(Length::points(200.f));
            surface().setCornerRadius(8.f);
            surface().setBorderWidth(1.f);

            auto titleLabel = std::make_shared<Label>(std::move(title));
            titleLabel->setFont(Font(17.f));
            titleLabel->setTextRole(TextRole::Secondary);
            addSubview(titleLabel);

            m_list = std::make_shared<Panel>();
            m_list->setBackgroundColor(Color(0x00000000));
            m_list->layout().setFlexDirection(FlexDirection::Column);
            m_list->layout().setGap(6.f);
            m_list->layout().setAlignItems(Align::Start);
            addSubview(m_list);

            applyStyle();
            rebuild();
        }

        void addChip(const std::string &label) {
            m_chips.push_back(label);
            rebuild();
        }
        void removeChip(const std::string &label) {
            m_chips.erase(std::remove(m_chips.begin(), m_chips.end(), label), m_chips.end());
            rebuild();
        }

        bool canAcceptDrop(const DragInfo &drag) override {
            return drag.payload.type == CHIP_TYPE;
        }
        void dragEntered(const DragInfo &) override {
            setDropHovered(true);
        }
        void dragExited() override {
            setDropHovered(false);
        }
        bool performDrop(const DragInfo &drag) override {
            setDropHovered(false);
            auto *source = static_cast<DropZone *>(drag.payload.source);
            const std::string label = drag.payload.ghostText; // a copy taken before the source is mutated
            if (source == this) { return true; }              // into its own zone — no changes
            if (source) { source->removeChip(label); }
            addChip(label);
            return true;
        }
        void themeChanged() override {
            applyStyle();
        }

    private:
        void setDropHovered(bool hovered) {
            m_dropHovered = hovered;
            applyStyle();
        }
        void applyStyle() {
            const Theme &t = Theme::current();
            setBackgroundColor(m_dropHovered ? t.controlHover : t.surfaceVariant);
            surface().setBorderColor(m_dropHovered ? t.accent : t.border);
        }
        void rebuild() {
            m_list->removeAllSubviews();
            for (const auto &label : m_chips) { m_list->addSubview(std::make_shared<DragChip>(label, this)); }
        }

        std::vector<std::string> m_chips;
        std::shared_ptr<Panel> m_list;
        bool m_dropHovered = false;
    };

} // namespace

DragDropSection::DragDropSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void DragDropSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Drag & Drop", "Dragging in the event system: source, drop target, ghost at the cursor."
    ));

    auto example =
        std::make_shared<DemoExample>("Moving between zones", "Drag a chip from one zone to another (Esc cancels).");

    auto row = std::make_shared<Panel>();
    row->setBackgroundColor(Color(0x00000000));
    row->layout().setFlexDirection(FlexDirection::Row);
    row->layout().setGap(12.f);
    row->layout().setWidth(Length::points(480.f));
    row->addSubview(std::make_shared<DropZone>("Zone A", std::vector<std::string>{"Apple", "Banana", "Cherry", "Date"}));
    row->addSubview(std::make_shared<DropZone>("Zone B", std::vector<std::string>{}));
    example->addContent(row);
    addSubview(example);
}

void DragDropSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void DragDropSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
