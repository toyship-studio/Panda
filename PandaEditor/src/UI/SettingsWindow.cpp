#include "UI/SettingsWindow.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float NAVIGATION_WIDTH = 190.f;
    constexpr float NAVIGATION_ROW = 32.f;

    std::shared_ptr<Panel> clearPanel() {
        auto panel = std::make_shared<Panel>();
        panel->setBackgroundColor(Color(0x00000000));
        return panel;
    }

} // namespace

SettingsWindowView::SettingsWindowView(std::vector<Page> pages, std::function<void()> onClose)
    : m_pages(std::move(pages))
    , m_onClose(std::move(onClose))
    , m_dirtyShown(m_pages.size(), false) {
    setFocusable(true); // Escape closes the window also with no field in focus
    setBackgroundColor(ThemeColor::windowBackground);
    layout().setFlexDirection(FlexDirection::Row);
    layout().setFlexGrow(1.f);
    layout().setWidth(Length::percent(100.f));
    layout().setHeight(Length::percent(100.f));

    // LEFT: the pages.
    auto side = std::make_shared<Panel>();
    side->setBackgroundColor(ThemeColor::surface);
    side->layout().setFlexDirection(FlexDirection::Column);
    side->layout().setWidth(Length::points(NAVIGATION_WIDTH));
    side->layout().setFlexShrink(0.f);
    side->layout().setPadding(Spacing::md);
    side->layout().setGap(Spacing::md);
    auto caption = std::make_shared<Label>("SETTINGS");
    caption->setFont(Typo::PanelTitle);
    caption->setTextRole(TextRole::Muted);
    caption->layout().setMargin(Edge::Left, Spacing::md);
    caption->layout().setMargin(Edge::Top, Spacing::sm);
    side->addSubview(caption);

    m_navigation = std::make_shared<ListView>();
    ListViewAppearance appearance = m_navigation->appearance();
    appearance.rowHeight = NAVIGATION_ROW;
    appearance.rowRadius = Radius::sm;
    m_navigation->setAppearance(appearance);
    m_navigation->setSelectionMode(ListSelectionMode::Single);
    m_navigation->layout().setFlexGrow(1.f);
    for (const Page &page : m_pages) {
        auto row = clearPanel();
        row->setUserInteractionEnabled(false);
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setAlignItems(Align::Center);
        row->layout().setGap(Spacing::md);
        row->layout().setFlexGrow(1.f);
        if (!page.icon.empty()) { row->addSubview(makeIcon(page.icon, Ic::RowSize)); }
        auto title = std::make_shared<Label>(page.title);
        title->setFont(Typo::Body);
        title->layout().setFlexGrow(1.f);
        row->addSubview(title);
        auto mark = std::make_shared<Label>("●"); // unsaved changes
        mark->setFont(Typo::Caption);
        mark->setTextRole(TextRole::Accent);
        mark->setHidden(true);
        row->addSubview(mark);
        m_dirtyMarks.push_back(mark);
        m_navigation->addRow(row);
    }
    m_navigation->setOnSelectionChanged([this](ListView &list) { selectPage(list.selectedIndex()); });
    side->addSubview(m_navigation);
    addSubview(side);

    // RIGHT: the title of the page, its document, the footer of unsaved changes.
    auto main = clearPanel();
    main->layout().setFlexDirection(FlexDirection::Column);
    main->layout().setFlexGrow(1.f);
    main->layout().setFlexShrink(1.f);
    main->layout().setFlexBasis(0.f);

    auto header = clearPanel();
    header->layout().setFlexDirection(FlexDirection::Column);
    header->layout().setPadding(Edge::Horizontal, Spacing::xl);
    header->layout().setPadding(Edge::Top, Spacing::lg);
    header->layout().setPadding(Edge::Bottom, Spacing::md);
    header->layout().setGap(2.f);
    m_title = std::make_shared<Label>("");
    m_title->setFont(Typo::DialogTitle);
    header->addSubview(m_title);
    m_hint = std::make_shared<Label>("");
    m_hint->setFont(Typo::Caption);
    m_hint->setTextRole(TextRole::Dim);
    header->addSubview(m_hint);
    main->addSubview(header);

    m_content = clearPanel();
    m_content->layout().setFlexDirection(FlexDirection::Column);
    m_content->layout().setFlexGrow(1.f);
    m_content->layout().setFlexShrink(1.f);
    m_content->layout().setFlexBasis(0.f);
    m_content->layout().setPadding(Edge::Horizontal, Spacing::md);
    main->addSubview(m_content);

    m_footer = std::make_shared<Panel>();
    m_footer->setBackgroundColor(ThemeColor::surface);
    m_footer->layout().setFlexDirection(FlexDirection::Row);
    m_footer->layout().setAlignItems(Align::Center);
    m_footer->layout().setGap(Spacing::md);
    m_footer->layout().setPadding(Edge::Horizontal, Spacing::xl);
    m_footer->layout().setPadding(Edge::Vertical, Spacing::md);
    m_footerText = std::make_shared<Label>("");
    m_footerText->setFont(Typo::Caption);
    m_footerText->setTextRole(TextRole::Secondary);
    m_footerText->layout().setFlexGrow(1.f);
    m_footerText->layout().setFlexShrink(1.f);
    m_footer->addSubview(m_footerText);
    auto revert = std::make_shared<Button>("Revert");
    revert->setOnClick([this](Button &) {
        if (Document *document = selectedDocument()) {
            document->revert();
            document->refresh();
        }
    });
    m_footer->addSubview(revert);
    auto save = std::make_shared<Button>("Save");
    save->setStyle(ButtonStyle::Accent);
    save->setOnClick([this](Button &) {
        if (Document *document = selectedDocument()) { document->save(); }
    });
    m_footer->addSubview(save);
    m_footer->setCollapsed(true);
    main->addSubview(m_footer);
    addSubview(main);

    if (!m_pages.empty()) { m_navigation->setSelectedIndex(0); }
    selectPage(m_pages.empty() ? -1 : 0);
}

Document *SettingsWindowView::selectedDocument() const {
    return m_selected >= 0 && m_selected < static_cast<int>(m_pages.size()) ? m_pages[static_cast<size_t>(m_selected)].document
                                                                           : nullptr;
}

void SettingsWindowView::selectPage(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size()) || index == m_selected) { return; }
    m_selected = index;
    if (m_navigation->selectedIndex() != index) { m_navigation->setSelectedIndex(index); }
    const Page &page = m_pages[static_cast<size_t>(index)];
    m_title->setText(page.title);
    m_hint->setText(page.hint);
    m_content->removeAllSubviews();
    if (page.document != nullptr) { m_content->addSubview(page.document->view()); }
    syncFooter();
    setNeedsLayout();
}

void SettingsWindowView::update(double deltaTime) {
    Panel::update(deltaTime);
    syncFooter();
}

void SettingsWindowView::syncFooter() {
    for (size_t i = 0; i < m_pages.size(); ++i) {
        const bool dirty = m_pages[i].document != nullptr && m_pages[i].document->isDirty();
        if (dirty == m_dirtyShown[i]) { continue; }
        m_dirtyShown[i] = dirty;
        m_dirtyMarks[i]->setHidden(!dirty);
    }
    Document *document = selectedDocument();
    const bool dirty = document != nullptr && document->isDirty();
    if (dirty == m_footerShown) { return; }
    m_footerShown = dirty;
    if (dirty) {
        m_footerText->setText("Unsaved changes in " + document->filePath().filename().string() + ": they apply when saved.");
    }
    m_footer->setCollapsed(!dirty);
    setNeedsLayout();
}

bool SettingsWindowView::keyDown(KeyEvent &event) {
    if (event.key == Key::Escape) {
        if (m_onClose) { m_onClose(); }
        return true;
    }
    return Panel::keyDown(event);
}

void SettingsWindowView::contextAttached() {
    Context *context = getContext();
    if (context == nullptr) { return; }
    // A key nothing took (no view has the keyboard, or the focused one let it go): the context
    // is this window's alone.
    context->setUnhandledKeyDownHandler([this](const KeyEvent &event) {
        if (event.key != Key::Escape) { return false; }
        if (m_onClose) { m_onClose(); }
        return true;
    });
}

} // namespace Editor
