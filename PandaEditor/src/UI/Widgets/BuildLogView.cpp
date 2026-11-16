#include "UI/Widgets/BuildLogView.hpp"

#include <PandaEditorKit/Widgets/Typography.hpp>

#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float ROW_HEIGHT = 22.f; // a dense log (the engine console — 26)
} // namespace

// A line of the output: monospace text, the color by the kind of the line. Reused (virtualization).
class BuildLogView::Cell final : public Panel {
public:
    Cell()
        : m_text(std::make_shared<Label>("")) {
        setBackgroundColor(Color(0x00000000));
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setPadding(Edge::Horizontal, 10.f);
        m_text->setFont(Typo::ValueSmall);
        m_text->setClipsToBounds(true);
        m_text->layout().setFlexGrow(1.f);
        m_text->layout().setFlexShrink(1.f);
        addSubview(m_text);
    }

    void bind(const std::string &text, LineKind kind) {
        Font font = Typo::ValueSmall;
        ThemeColor color = ThemeColor::textSecondary;
        switch (kind) {
            case LineKind::Command:
                font = font.withWeight(FontWeight::Medium);
                color = ThemeColor::textPrimary;
                break;
            case LineKind::Plain: break;
            case LineKind::Warning: color = ThemeColor::warning; break;
            case LineKind::Error: color = ThemeColor::danger; break;
            case LineKind::StatusOk:
                font = font.withWeight(FontWeight::Medium);
                color = ThemeColor::success;
                break;
            case LineKind::StatusFail:
                font = font.withWeight(FontWeight::Medium);
                color = ThemeColor::danger;
                break;
        }
        m_text->setFont(font);
        m_text->setTextColor(color);
        m_text->setText(text);
    }

private:
    std::shared_ptr<Label> m_text;
};

BuildLogView::BuildLogView() {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Column);
    m_list = std::make_shared<CollectionView>(std::make_shared<ListCollectionLayout>(ROW_HEIGHT));
    m_list->setAccessibilityId("lines");
    m_list->setSelectionMode(CollectionSelectionMode::None);
    m_list->layout().setFlexGrow(1.f);
    CollectionViewDataSource source;
    source.numberOfItems = [this] { return static_cast<int>(m_lines.size()); };
    source.cellForReuse = [] { return std::make_shared<Cell>(); };
    source.configureCell = [this](View &cell, int index) {
        const Line &line = m_lines[index];
        static_cast<Cell &>(cell).bind(line.text, line.kind);
    };
    source.titleForItem = [this](int index) { return m_lines[index].text; };
    m_list->setDataSource(source);
    addSubview(m_list);
}

void BuildLogView::clear() {
    m_lines.clear();
    reload();
}

void BuildLogView::append(const std::vector<std::string> &lines) {
    for (const std::string &text : lines) { m_lines.push_back({text, classify(text)}); }
    reload();
}

void BuildLogView::append(std::string text, LineKind kind) {
    m_lines.push_back({std::move(text), kind});
    reload();
}

void BuildLogView::reload() {
    m_list->reloadData();
    m_list->scrollToBottom();
}

BuildLogView::LineKind BuildLogView::classify(const std::string &text) {
    if (text.rfind("> ", 0) == 0) { return LineKind::Command; }
    if (text.find("error") != std::string::npos || text.find("Error") != std::string::npos) { return LineKind::Error; }
    if (text.find("warning") != std::string::npos || text.find("Warning") != std::string::npos) {
        return LineKind::Warning;
    }
    return LineKind::Plain;
}

} // namespace Editor
