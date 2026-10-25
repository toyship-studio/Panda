#include "UI/Panels/StatisticsPanel.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <memory>
#include <string>
#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {

    // Thin row divider (design: border-bottom #1f1f1f under each metric).
    std::shared_ptr<Panel> thinDivider() {
        auto divider = std::make_shared<Panel>();
        divider->setUserInteractionEnabled(false);
        divider->layout().setHeight(Length::points(1.f));
        divider->setThemeStyle([](View &view) { view.setBackgroundColor(theme().borderFaint); });
        return divider;
    }

    // Table section title: quiet uppercase caps on a flat background (design Statistics/World Settings).
    void styleCapsSection(CollapsibleSection &section) {
        section.setTitleFont(Typo::SectionCaps);
        section.setThemeStyle([](View &view) { static_cast<CollapsibleSection &>(view).setTitleColor(theme().textDim); });
        section.setTitleLetterSpacing(Typo::SectionCapsTracking);
        section.setHeaderFill(false);
    }

    // A "metric — mono value" row + a thin bottom divider.
    std::shared_ptr<Panel> statRow(const std::string &name, const std::shared_ptr<Label> &value) {
        auto wrap = std::make_shared<Panel>();
        wrap->setBackgroundColor(Color(0x00000000));
        wrap->layout().setFlexDirection(FlexDirection::Column);

        auto row = std::make_shared<Panel>();
        row->setBackgroundColor(Color(0x00000000));
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setAlignItems(Align::Center);
        row->layout().setPadding(Edge::Vertical, 5.f);

        auto label = std::make_shared<Label>(name);
        label->setFont(Typo::Caption);
        label->setTextRole(TextRole::Muted);
        label->layout().setFlexGrow(1.f); // pushes the value to the right edge

        value->setFont(Typo::Value); // monospaced value (design)

        row->addSubview(label);
        row->addSubview(value);
        wrap->addSubview(row);
        wrap->addSubview(thinDivider());
        return wrap;
    }

    std::shared_ptr<Label> makeValue(const std::string &text) {
        return std::make_shared<Label>(text);
    }

} // namespace

StatisticsPanelView::StatisticsPanelView() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void StatisticsPanelView::addSubviews() {
    auto body = std::make_shared<ScrollView>();
    body->setScrollDirection(ScrollDirection::Vertical);
    body->layout().setFlexGrow(1.f);
    body->getContentView()->layout().setFlexDirection(FlexDirection::Column);
    body->getContentView()->layout().setPadding(Edge::Horizontal, 12.f);
    body->getContentView()->layout().setPadding(Edge::Vertical, 12.f);
    body->getContentView()->layout().setGap(8.f);
    addSubview(body);

    // Live metrics: entities are supplied by the coordinator, per-frame render metrics — by EditorLayer.
    auto scene = std::make_shared<CollapsibleSection>("SCENE");
    styleCapsSection(*scene);
    m_entityCount = makeValue("0");
    scene->addContent(statRow("Entities", m_entityCount));
    body->addContentSubview(scene);

    auto renderer = std::make_shared<CollapsibleSection>("RENDERER");
    styleCapsSection(*renderer);
    m_fps = makeValue("0");
    m_drawCalls3d = makeValue("0");
    m_shadowDrawCalls = makeValue("0");
    m_drawCalls2d = makeValue("0");
    m_quads = makeValue("0");
    m_vertices = makeValue("0");
    m_indices = makeValue("0");
    renderer->addContent(statRow("FPS", m_fps));
    renderer->addContent(statRow("Draw Calls 3D", m_drawCalls3d));
    renderer->addContent(statRow("Shadow Draw Calls", m_shadowDrawCalls));
    renderer->addContent(statRow("Draw Calls 2D", m_drawCalls2d));
    renderer->addContent(statRow("Quads", m_quads));
    renderer->addContent(statRow("Vertices", m_vertices));
    renderer->addContent(statRow("Indices", m_indices));
    body->addContentSubview(renderer);

    auto ui = std::make_shared<CollapsibleSection>("PANDA UI");
    styleCapsSection(*ui);
    m_uiLayouts = makeValue("0");
    ui->addContent(statRow("Layout Passes / s", m_uiLayouts));
    body->addContentSubview(ui);

    // Contribution sections (metrics of module systems: flora etc.) — after the core sections.
    m_extensionSections = std::make_shared<Panel>();
    m_extensionSections->setBackgroundColor(Color(0x00000000));
    m_extensionSections->layout().setFlexDirection(FlexDirection::Column);
    m_extensionSections->layout().setGap(8.f);
    body->addContentSubview(m_extensionSections);
    rebuildExtensionSections();
}

void StatisticsPanelView::rebuildExtensionSections() {
    m_extensionSections->removeAllSubviews();
    m_extensions.clear();
    for (const StatisticsSection &contribution : editorExtensions().statistics) {
        auto section = std::make_shared<CollapsibleSection>(contribution.title);
        styleCapsSection(*section);
        ExtensionSection entry;
        for (const std::string &row : contribution.rows) {
            auto value = makeValue("0");
            section->addContent(statRow(row, value));
            entry.values.push_back(value);
        }
        entry.last.assign(contribution.rows.size(), UINT32_MAX);
        entry.scratch.assign(contribution.rows.size(), 0);
        m_extensions.push_back(std::move(entry));
        m_extensionSections->addSubview(section);
    }
    setNeedsLayout();
}

void StatisticsPanelView::collectExtensions() {
    const std::vector<StatisticsSection> &contributions = editorExtensions().statistics;
    for (size_t i = 0; i < m_extensions.size() && i < contributions.size(); i++) {
        ExtensionSection &entry = m_extensions[i];
        if (contributions[i].collect == nullptr || entry.scratch.empty()) { continue; }
        contributions[i].collect(entry.scratch.data());
        for (size_t row = 0; row < entry.values.size(); row++) {
            if (entry.scratch[row] != entry.last[row]) {
                entry.last[row] = entry.scratch[row];
                entry.values[row]->setText(std::to_string(entry.scratch[row]));
            }
        }
    }
}

void StatisticsPanelView::setFrameStatistics(
    int fps, uint32_t drawCalls3d, uint32_t shadowDrawCalls, uint32_t drawCalls2d, uint32_t quads,
    uint32_t uiLayouts
) {
    auto put = [](const std::shared_ptr<Label> &label, uint32_t value, uint32_t &last) {
        if (label && value != last) {
            last = value;
            label->setText(std::to_string(value));
        }
    };
    put(m_fps, static_cast<uint32_t>(fps < 0 ? 0 : fps), m_lastFps);
    put(m_drawCalls3d, drawCalls3d, m_lastDrawCalls3d);
    put(m_shadowDrawCalls, shadowDrawCalls, m_lastShadowDrawCalls);
    put(m_drawCalls2d, drawCalls2d, m_lastDrawCalls2d);
    const uint32_t lastQuads = m_lastQuads;
    put(m_quads, quads, m_lastQuads);
    if (quads != lastQuads) {
        // Derived values of the 2D batcher: a quad = 4 vertices and 6 indices.
        if (m_vertices) { m_vertices->setText(std::to_string(quads * 4)); }
        if (m_indices) { m_indices->setText(std::to_string(quads * 6)); }
    }
    put(m_uiLayouts, uiLayouts, m_lastUiLayouts);
}

void StatisticsPanelView::setEntityCount(int count) {
    if (m_entityCount) { m_entityCount->setText(std::to_string(count)); }
}

void StatisticsPanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void StatisticsPanelView::setupStyle() {
    setBackgroundColor(theme().windowBackground);
}

} // namespace Editor
