#include "UI/Widgets/BuildLogView.hpp"

#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float ROW_HEIGHT = 22.f; // a dense log (the engine console — 26)
} // namespace

// A line of the output: monospace text, the color by the kind of the line. Reused (virtualization).
// A line that names a place in a file is three pieces in a row: what stands before the place, the
// place as a link, and the rest.
class BuildLogView::Cell final : public Panel {
public:
    Cell()
        : m_text(std::make_shared<Label>(""))
        , m_place(std::make_shared<Button>(""))
        , m_rest(std::make_shared<Label>("")) {
        setBackgroundColor(Color(0x00000000));
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setPadding(Edge::Horizontal, 10.f);
        m_text->setFont(Typo::ValueSmall);
        m_text->setClipsToBounds(true);
        addSubview(m_text);
        // The place: a link of the log's own font, with no plate and no room around it.
        ButtonAppearance link = Button::defaultAppearance(ButtonStyle::Link);
        link.font = Typo::ValueSmall;
        link.padding = EdgeInsets();
        link.minHeight = 0.f;
        link.borderWidth = 0.f;
        m_place->setAppearance(link);
        m_place->setCursor(Cursor::PointingHand);
        m_place->setFocusable(false); // the keys of the panel are not the link's
        m_place->setTooltip("Open in the external editor");
        m_place->setAccessibilityId("place"); // a UI test finds the link of a line by it
        m_place->layout().setFlexShrink(0.f);
        m_place->setOnClick([this](Button &) {
            if (m_line > 0) { editorHost().openInExternalEditor(m_file, m_line, m_column); }
        });
        addSubview(m_place);
        m_rest->setFont(Typo::ValueSmall);
        m_rest->setClipsToBounds(true);
        m_rest->layout().setFlexGrow(1.f);
        m_rest->layout().setFlexShrink(1.f);
        addSubview(m_rest);
    }

    void bind(const Line &line) {
        Font font = Typo::ValueSmall;
        ThemeColor color = ThemeColor::textSecondary;
        switch (line.kind) {
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
        const bool placed = line.place.has_value();
        m_text->setFont(font);
        m_text->setTextColor(color);
        m_rest->setTextColor(color);
        if (placed) {
            const Panda::BuildOutputLocation &place = *line.place;
            m_text->setText(line.text.substr(0, place.begin));
            m_place->setText(line.text.substr(place.begin, place.end - place.begin));
            m_rest->setText(line.text.substr(place.end));
            m_file = place.file;
            m_line = place.line;
            m_column = place.column;
        } else {
            m_text->setText(line.text);
            m_line = 0;
        }
        // A line with no place is its text alone, as wide as the row.
        m_text->layout().setFlexGrow(placed ? 0.f : 1.f);
        m_text->layout().setFlexShrink(placed ? 0.f : 1.f);
        for (View *piece : {static_cast<View *>(m_place.get()), static_cast<View *>(m_rest.get())}) {
            piece->setHidden(!placed);
            piece->layout().setDisplay(placed ? Display::Flex : Display::None);
        }
    }

private:
    std::shared_ptr<Label> m_text;
    std::shared_ptr<Button> m_place;
    std::shared_ptr<Label> m_rest;
    Panda::path_t m_file; // the place the link of the line opens
    int m_line = 0;
    int m_column = 0;
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
        static_cast<Cell &>(cell).bind(m_lines[index]);
    };
    source.titleForItem = [this](int index) { return m_lines[index].text; };
    m_list->setDataSource(source);
    addSubview(m_list);
}

void BuildLogView::setFolders(std::vector<Panda::path_t> folders) {
    m_folders = std::move(folders);
}

void BuildLogView::clear() {
    m_lines.clear();
    reload();
}

void BuildLogView::append(const std::vector<std::string> &lines) {
    for (const std::string &text : lines) {
        const LineKind kind = classify(text);
        // What a tool said may name a place in a file; a command the editor ran does not.
        m_lines.push_back({text, kind, kind == LineKind::Command ? std::nullopt : Panda::findBuildOutputLocation(text, m_folders)});
    }
    reload();
}

void BuildLogView::append(std::string text, LineKind kind) {
    m_lines.push_back({std::move(text), kind, std::nullopt});
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
