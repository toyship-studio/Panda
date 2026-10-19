#include "UI/Panels/OutputPanel.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>
#include "PandaUI/Elements/Control.hpp"

#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float ROW_HEIGHT = 26.f;

    std::string nowTimestamp() {
        std::time_t now = std::time(nullptr);
        std::tm *tm = std::localtime(&now);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
        return buf;
    }

    Color levelColor(OutputPanelView::LogLevel level) {
        if (level == OutputPanelView::LogLevel::Warning) { return theme().warning; }
        if (level == OutputPanelView::LogLevel::Error) { return theme().danger; }
        return Color(0x7FB2D6FF); // info — muted light blue (design)
    }

    const char *levelIcon(OutputPanelView::LogLevel level) {
        if (level == OutputPanelView::LogLevel::Warning) { return Ic::Warning; }
        if (level == OutputPanelView::LogLevel::Error) { return Ic::Error; }
        return Ic::Info;
    }

    // Log row: timestamp (dim mono) + severity icon (colored) + monospace message. Reused (virtualized).
    class LogCell final : public Panel {
    public:
        LogCell()
            : m_time(std::make_shared<Label>(""))
            , m_icon(std::make_shared<ImageView>())
            , m_message(std::make_shared<Label>("")) {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Start); // multi-line text — the row header is at the top
            layout().setGap(8.f);
            layout().setPadding(Edge::Horizontal, 10.f);
            layout().setPadding(Edge::Vertical, 4.f);

            m_time->setFont(Typo::ValueSmall);
            m_time->setTextRole(TextRole::Dim);
            m_time->layout().setFlexShrink(0.f);

            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_icon->layout().setSize({14.f, 14.f});
            m_icon->layout().setFlexShrink(0.f);

            m_message->setFont(Typo::Value);
            m_message->setClipsToBounds(true);
            m_message->setNumberOfLines(0); // wrap long lines (the height comes from the list layout)
            m_message->layout().setFlexGrow(1.f);
            m_message->layout().setFlexShrink(1.f);

            addSubview(m_time);
            addSubview(m_icon);
            addSubview(m_message);
        }
        void bind(const std::string &time, const std::string &icon, Color color, const std::string &message) {
            m_time->setText(time);
            m_icon->setTexture(icons().get(icon));
            m_icon->setTintColor(color);
            m_message->setText(message);
        }

    private:
        std::shared_ptr<Label> m_time;
        std::shared_ptr<ImageView> m_icon;
        std::shared_ptr<Label> m_message;
    };

} // namespace

// Clickable severity filter chip: count label + tinted bg; dim when its level is filtered out.
class SeverityChip final : public Control {
public:
    explicit SeverityChip(Color color)
        : m_color(color)
        , m_label(std::make_shared<Label>("")) {
        setCursor(Cursor::PointingHand);
        setFocusable(false);
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setPadding(Edge::Horizontal, 9.f);
        layout().setPadding(Edge::Vertical, 3.f);
        surface().setCornerRadius(6.f);
        m_label->setFont(Typo::Meta);
        addSubview(m_label);
        applyStyle();
    }
    void setText(const std::string &text) {
        m_label->setText(text);
    }
    void setShown(bool shown) {
        m_shown = shown;
        applyStyle();
    }
    void setOnToggle(std::function<void()> callback) {
        m_onToggle = std::move(callback);
    }

protected:
    void performPrimaryAction() override {
        if (m_onToggle) { m_onToggle(); }
    }
    void themeChanged() override {
        applyStyle();
    }

private:
    void applyStyle() {
        // shown → tinted bg + colored text; filtered out → faint bg + dim text.
        setBackgroundColor(Color(m_color.r, m_color.g, m_color.b, m_shown ? 0.12f : 0.04f));
        m_label->setTextColor(m_shown ? m_color : theme().textDim);
    }

    Color m_color;
    std::shared_ptr<Label> m_label;
    bool m_shown = true;
    std::function<void()> m_onToggle;
};

OutputPanelView *OutputPanelView::s_instance = nullptr;

OutputPanelView::OutputPanelView() {
    s_instance = this;
    addSubviews();
    setupLayout();
    setupStyle();
}

OutputPanelView::~OutputPanelView() {
    if (s_instance == this) { s_instance = nullptr; }
}

void OutputPanelView::loggerCallback(std::string_view message, Foundation::EditorLogger::MessageType type) {
    if (!s_instance) { return; }
    LogLevel level = type == Foundation::EditorLogger::Error     ? LogLevel::Error
                   : type == Foundation::EditorLogger::Warning ? LogLevel::Warning
                                                                : LogLevel::Info;
    s_instance->appendLog(level, std::string(message));
}

void OutputPanelView::addSubviews() {
    // Slim bar: Clear icon + severity chips (counter + filter on click). The panel name is provided
    // by the tab.
    auto bar = std::make_shared<Panel>();
    bar->setBackgroundColor(Color(0x00000000));
    bar->layout().setFlexDirection(FlexDirection::Row);
    bar->layout().setAlignItems(Align::Center);
    bar->layout().setFlexShrink(0.f);
    bar->layout().setGap(6.f);
    bar->layout().setPadding(Edge::Horizontal, 8.f);
    bar->layout().setPadding(Edge::Vertical, 4.f);

    auto clear = std::make_shared<IconButton>(Ic::Clear, Ic::RowSize, 24.f);
    clear->setCornerRadius(5.f);
    clear->setTooltip("Clear");
    clear->setOnClick([this] { clearLog(); });
    bar->addSubview(clear);

    auto copy = std::make_shared<IconButton>(Ic::Copy, Ic::RowSize, 24.f);
    copy->setCornerRadius(5.f);
    copy->setTooltip("Copy all");
    copy->setOnClick([this] { copyAll(); });
    bar->addSubview(copy);

    m_infoChip = std::make_shared<SeverityChip>(Color(0x7FB2D6FF));
    m_infoChip->setOnToggle([this] { toggleLevel(LogLevel::Info); });
    bar->addSubview(m_infoChip);
    m_warnChip = std::make_shared<SeverityChip>(theme().warning);
    m_warnChip->setOnToggle([this] { toggleLevel(LogLevel::Warning); });
    bar->addSubview(m_warnChip);
    m_errorChip = std::make_shared<SeverityChip>(theme().danger);
    m_errorChip->setOnToggle([this] { toggleLevel(LogLevel::Error); });
    bar->addSubview(m_errorChip);
    addSubview(bar);

    // A virtualized list of log rows (by the filtered indices m_visible);
    // the row height follows the actual text wrapping into the available width.
    m_rowLayout = std::make_shared<VariableListCollectionLayout>(
        [this](int index, float width) { return measureRowHeight(index, width); }
    );
    m_list = std::make_shared<CollectionView>(m_rowLayout);
    m_list->setSelectionMode(CollectionSelectionMode::None);
    m_list->layout().setFlexGrow(1.f);

    CollectionViewDataSource source;
    source.numberOfItems = [this] { return static_cast<int>(m_visible.size()); };
    source.cellForReuse = [] { return std::make_shared<LogCell>(); };
    source.configureCell = [this](View &cell, int index) {
        const LogEntry &entry = m_entries[m_visible[index]];
        static_cast<LogCell &>(cell).bind(entry.time, levelIcon(entry.level), levelColor(entry.level), entry.text);
    };
    m_list->setDataSource(source);
    addSubview(m_list);

    rebuildVisible();
    updateCounts();
}

void OutputPanelView::appendLog(LogLevel level, std::string message) {
    m_entries.push_back({level, std::move(message), nowTimestamp()});
    rebuildVisible();
    updateCounts();
    m_rowLayout->invalidate();
    m_list->reloadData();
    m_list->scrollToBottom();
}

void OutputPanelView::clearLog() {
    m_entries.clear();
    rebuildVisible();
    updateCounts();
    m_rowLayout->invalidate();
    m_list->reloadData();
}

bool OutputPanelView::levelShown(LogLevel level) const {
    if (level == LogLevel::Warning) { return m_showWarning; }
    if (level == LogLevel::Error) { return m_showError; }
    return m_showInfo;
}

void OutputPanelView::rebuildVisible() {
    m_visible.clear();
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (levelShown(m_entries[i].level)) { m_visible.push_back(i); }
    }
}

void OutputPanelView::toggleLevel(LogLevel level) {
    if (level == LogLevel::Warning) {
        m_showWarning = !m_showWarning;
    } else if (level == LogLevel::Error) {
        m_showError = !m_showError;
    } else {
        m_showInfo = !m_showInfo;
    }
    m_infoChip->setShown(m_showInfo);
    m_warnChip->setShown(m_showWarning);
    m_errorChip->setShown(m_showError);
    rebuildVisible();
    m_list->reloadData();
}

void OutputPanelView::updateCounts() {
    int info = 0, warn = 0, error = 0;
    for (const LogEntry &entry : m_entries) {
        if (entry.level == LogLevel::Warning) {
            ++warn;
        } else if (entry.level == LogLevel::Error) {
            ++error;
        } else {
            ++info;
        }
    }
    m_infoChip->setText("Info " + std::to_string(info));
    m_warnChip->setText("Warnings " + std::to_string(warn));
    m_errorChip->setText("Errors " + std::to_string(error));
}

void OutputPanelView::seedDemoLogs() {
    const std::string time = nowTimestamp();
    m_entries.push_back({LogLevel::Info, "Editor initialized.", time});
    m_entries.push_back({LogLevel::Info, "Loaded scene: Main.scene", time});
    m_entries.push_back({LogLevel::Warning, "Material 'Glass' has no albedo texture.", time});
    m_entries.push_back({LogLevel::Info, "Compiled 4 scripts in 0.3s.", time});
    m_entries.push_back({LogLevel::Error, "NullReference in PlayerController.cs:42", time});
}

void OutputPanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void OutputPanelView::setupStyle() {
    setBackgroundColor(theme().windowBackground);
}

void OutputPanelView::copyAll() {
    std::string all;
    for (const LogEntry &entry : m_entries) {
        all += "[";
        all += entry.time;
        all += "] ";
        all += entry.level == LogLevel::Error ? "ERROR "
               : entry.level == LogLevel::Warning ? "WARN  "
                                                  : "INFO  ";
        all += entry.text;
        all += "\n";
    }
    setClipboardText(all);
}

float OutputPanelView::measureRowHeight(int index, float width) {
    // Text width = viewport − row paddings − time − icon − gaps (LogCell).
    const LogEntry &entry = m_entries[m_visible[index]];
    Context *context = getContext();
    std::shared_ptr<FontAtlas> atlas =
        context ? context->getFontAtlas(Typo::Value.weight, Typo::Value.family) : nullptr;
    if (atlas == nullptr) { return ROW_HEIGHT; }
    if (m_timeColumnWidth <= 0.f) {
        TextLayoutOptions timeOptions;
        timeOptions.fontSize = Typo::ValueSmall.size;
        timeOptions.maxWidth = 10000.f;
        m_timeColumnWidth = TextLayout::measure(*atlas, "00:00:00", timeOptions).width;
    }
    const float textWidth = width - 10.f * 2.f - m_timeColumnWidth - 14.f - 8.f * 2.f;
    if (textWidth <= 20.f) { return ROW_HEIGHT; }
    TextLayoutOptions options;
    options.fontSize = Typo::Value.size;
    options.maxWidth = textWidth;
    options.numberOfLines = 0;
    options.overflowMode = TextOverflowMode::Clip;
    const Size size = TextLayout::measure(*atlas, entry.text, options);
    return std::max(ROW_HEIGHT, size.height + 8.f);
}

} // namespace Editor
