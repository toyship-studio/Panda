#include "UI/Panels/SettingsSectionsView.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

namespace Editor {

using namespace PandaUI;

namespace {

    // A settings section: a caps title on a flat background.
    std::shared_ptr<CollapsibleSection> capsSection(const std::string &title) {
        auto section = std::make_shared<CollapsibleSection>(title);
        section->setTitleFont(Typo::ComponentTitle);
        section->setTitleColor(ThemeColor::textMuted);
        section->setTitleLetterSpacing(Typo::ComponentTitleTracking);
        section->setHeaderFill(false);
        return section;
    }

} // namespace

SettingsSectionsView::SettingsSectionsView(Panda::SettingsScope scope)
    : m_scope(scope) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void SettingsSectionsView::addSubviews() {
    m_body = std::make_shared<ScrollView>();
    m_body->setScrollDirection(ScrollDirection::Vertical);
    m_body->layout().setFlexGrow(1.f);
    m_body->getContentView()->layout().setFlexDirection(FlexDirection::Column);
    m_body->getContentView()->layout().setPadding(Edge::Horizontal, 4.f);
    addSubview(m_body);

    m_sections = std::make_shared<Panel>();
    m_sections->setBackgroundColor(Color(0x00000000));
    m_sections->layout().setFlexDirection(FlexDirection::Column);
    m_body->addContentSubview(m_sections);
}

void SettingsSectionsView::setStore(Panda::TypeContext *store) {
    m_store = store;
    rebuild();
}

void SettingsSectionsView::setTitleFilter(std::function<bool(const std::string &)> filter) {
    m_filter = std::move(filter);
    rebuild();
}

void SettingsSectionsView::rebuild() {
    for (const auto &[title, section] : m_byTitle) { m_expanded[title] = section->isExpanded(); }
    m_byTitle.clear();
    m_sections->removeAllSubviews();
    if (m_store == nullptr) {
        setNeedsLayout();
        return;
    }
    for (const SettingsSection &contribution : editorExtensions().settingsSections) {
        if (contribution.scope != m_scope) { continue; }
        if (m_filter && !m_filter(contribution.title)) { continue; }
        auto section = capsSection(contribution.title);
        if (contribution.build) { contribution.build(*section, *m_store); }
        const auto expanded = m_expanded.find(contribution.title);
        if (expanded != m_expanded.end()) { section->setExpanded(expanded->second); }
        m_sections->addSubview(section);
        m_byTitle[contribution.title] = section;
    }
    setNeedsLayout();
}

void SettingsSectionsView::revealSection(const std::string &title) {
    const auto it = m_byTitle.find(title);
    if (it == m_byTitle.end()) { return; }
    it->second->setExpanded(true);
    // The sections above it make up the offset; the frames are ready after the next layout.
    float offset = 0.f;
    for (const auto &child : m_sections->getSubviews()) {
        if (child == it->second) { break; }
        offset += child->getFrame().size.height;
    }
    m_body->setContentOffset(Point(0.f, offset));
    setNeedsLayout();
}

void SettingsSectionsView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setFlexGrow(1.f);
}

void SettingsSectionsView::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Editor
