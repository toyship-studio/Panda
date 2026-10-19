#include "UI/Panels/ProfilerPanel.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <cstdio>
#include <memory>
#include <string>

namespace Editor {

using namespace PandaUI;

namespace {

    std::shared_ptr<Panel> thinDivider() {
        auto divider = std::make_shared<Panel>();
        divider->setUserInteractionEnabled(false);
        divider->layout().setHeight(Length::points(1.f));
        divider->setBackgroundColor(theme().borderFaint);
        return divider;
    }

} // namespace

ProfilerPanelView::ProfilerPanelView() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ProfilerPanelView::addSubviews() {
    auto body = std::make_shared<ScrollView>();
    body->setScrollDirection(ScrollDirection::Vertical);
    body->layout().setFlexGrow(1.f);
    body->getContentView()->layout().setFlexDirection(FlexDirection::Column);
    body->getContentView()->layout().setPadding(Edge::Horizontal, 12.f);
    body->getContentView()->layout().setPadding(Edge::Vertical, 12.f);
    addSubview(body);

    // Header: full frame by frameMark + copying the whole snapshot.
    auto header = std::make_shared<Panel>();
    header->setBackgroundColor(Color(0x00000000));
    header->layout().setFlexDirection(FlexDirection::Row);
    header->layout().setAlignItems(Align::Center);
    header->layout().setGap(8.f);

    m_frameLabel = std::make_shared<Label>("frame — ms");
    m_frameLabel->setFont(Typo::Value);
    m_frameLabel->layout().setPadding(Edge::Vertical, 5.f);
    m_frameLabel->layout().setFlexGrow(1.f);
    header->addSubview(m_frameLabel);

    auto copy = std::make_shared<IconButton>(Ic::Copy, Ic::RowSize, 24.f);
    copy->setCornerRadius(5.f);
    copy->setTooltip("Copy all");
    copy->setOnClick([this] { copyAll(); });
    header->addSubview(copy);

    body->addContentSubview(header);
    body->addContentSubview(thinDivider());

    for (Row &row : m_rows) {
        row.container = std::make_shared<Panel>();
        row.container->setBackgroundColor(Color(0x00000000));
        row.container->layout().setFlexDirection(FlexDirection::Column);

        auto line = std::make_shared<Panel>();
        line->setBackgroundColor(Color(0x00000000));
        line->layout().setFlexDirection(FlexDirection::Row);
        line->layout().setAlignItems(Align::Center);
        line->layout().setPadding(Edge::Vertical, 4.f);

        row.name = std::make_shared<Label>("");
        row.name->setFont(Typo::Caption);
        row.name->setTextRole(TextRole::Muted);
        row.name->setClipsToBounds(true);
        row.name->layout().setFlexGrow(1.f);

        row.value = std::make_shared<Label>("");
        row.value->setFont(Typo::Value);

        line->addSubview(row.name);
        line->addSubview(row.value);
        row.container->addSubview(line);
        row.container->addSubview(thinDivider());
        row.container->setHidden(true);
        row.container->layout().setDisplay(Display::None);
        body->addContentSubview(row.container);
    }
}

void ProfilerPanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setWidth(Length::percent(100.f));
    layout().setHeight(Length::percent(100.f));
}

void ProfilerPanelView::setupStyle() {
    setBackgroundColor(theme().windowBackground);
}

void ProfilerPanelView::updateSnapshot() {
    // A hidden panel (inactive tab/closed) does not reset the texts — otherwise
    // Layout Passes / s fluctuates in the background even at rest.
    if (isHidden()) { return; }
    Foundation::Profiler::Entry entries[MAX_ROWS * 4];
    const size_t count = Foundation::Profiler::snapshot(entries, MAX_ROWS * 4);

    char buffer[160];
    std::snprintf(
        buffer, sizeof(buffer), "frame %.2f ms", Foundation::Profiler::lastFrameMs()
    );
    m_frameLabel->setText(buffer);
    m_clipboardText.assign(buffer);
    m_clipboardText += "\n";

    size_t rowIndex = 0;
    for (size_t i = 0; i < count && rowIndex < MAX_ROWS; i++) {
        const Foundation::Profiler::Entry &entry = entries[i];
        Row &row = m_rows[rowIndex++];

        if (entry.calls == 0) { // plot value
            std::snprintf(buffer, sizeof(buffer), "%s  [%s]", entry.name, entry.thread);
            row.name->setText(buffer);
            std::snprintf(buffer, sizeof(buffer), "%.0f", entry.totalMs);
            row.value->setText(buffer);
            std::snprintf(
                buffer, sizeof(buffer), "%s [%s] = %.0f\n", entry.name, entry.thread,
                entry.totalMs
            );
            m_clipboardText += buffer;
        } else {
            // EMA over total: damps the per-frame noise but still reacts promptly.
            float &ema = m_emaTotals[entry.name];
            ema = ema == 0.f ? entry.totalMs : ema + (entry.totalMs - ema) * 0.2f;
            std::snprintf(buffer, sizeof(buffer), "%s  [%s]", entry.name, entry.thread);
            row.name->setText(buffer);
            std::snprintf(
                buffer, sizeof(buffer), "%.2f ms  ×%u  mx %.2f", ema, entry.calls, entry.maxMs
            );
            row.value->setText(buffer);
            std::snprintf(
                buffer, sizeof(buffer), "%s [%s]: %.2f ms x%u mx %.2f\n", entry.name,
                entry.thread, ema, entry.calls, entry.maxMs
            );
            m_clipboardText += buffer;
        }
        if (row.container->isHidden()) {
            row.container->setHidden(false);
            row.container->layout().setDisplay(Display::Flex);
        }
    }
    for (; rowIndex < MAX_ROWS; rowIndex++) {
        Row &row = m_rows[rowIndex];
        if (!row.container->isHidden()) {
            row.container->setHidden(true);
            row.container->layout().setDisplay(Display::None);
        }
    }
}

void ProfilerPanelView::copyAll() {
    setClipboardText(m_clipboardText);
}

} // namespace Editor
