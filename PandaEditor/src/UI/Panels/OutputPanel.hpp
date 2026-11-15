#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <Foundation/Logger.hpp>

#include <memory>
#include <string>
#include <vector>

namespace PandaUI {
class CollectionView;
class VariableListCollectionLayout;
}

namespace Editor {

class SeverityChip; // clickable severity filter chip (defined in .cpp)

// The Output panel: a virtualized list of log rows (time + level + text) with auto-scroll, a Clear
// button and filter chips by level (a click hides/shows the level). Output only (no input —
// hence not a "Console"). It is a view of the editor log and has no other source: whoever has a
// word for the user — the editor, a package, the engine — writes LOG_INFO_EDITOR /
// LOG_WARN_EDITOR / LOG_ERROR_EDITOR, and the line is here, in the history a tool reads (the
// `logs` of the MCP server) and in the console of an application without an editor.
class OutputPanelView final : public EditorView {
public:
    enum class LogLevel { Info, Warning, Error };

    OutputPanelView();

    ~OutputPanelView();

    void clearLog();
    void copyAll(); // the whole log to the clipboard

    // The sink of the editor log (Foundation::EditorLogger::init).
    static void loggerCallback(std::string_view message, Foundation::EditorLogger::MessageType type);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    void appendLog(LogLevel level, std::string message); // a row at the end (+auto-scroll)
    void rebuildVisible();          // rebuild the indices of visible rows by the level filter
    void toggleLevel(LogLevel level); // click on a chip — hide/show the level
    bool levelShown(LogLevel level) const;
    void updateCounts();            // counters on the chips

    struct LogEntry {
        LogLevel level;
        std::string text;
        std::string time; // HH:MM:SS
    };

    std::vector<LogEntry> m_entries;
    std::vector<int> m_visible; // indices of m_entries that passed the level filter
    bool m_showInfo = true;
    bool m_showWarning = true;
    bool m_showError = true;
    float measureRowHeight(int index, float width); // row height with text wrapping
    std::shared_ptr<PandaUI::CollectionView> m_list;
    std::shared_ptr<PandaUI::VariableListCollectionLayout> m_rowLayout;
    float m_timeColumnWidth = 0.f; // width of the time column (measured once)
    std::shared_ptr<SeverityChip> m_infoChip;
    std::shared_ptr<SeverityChip> m_warnChip;
    std::shared_ptr<SeverityChip> m_errorChip;

    static OutputPanelView *s_instance; // the active panel — the engine log receiver
};

} // namespace Editor
