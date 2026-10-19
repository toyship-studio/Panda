#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <Foundation/Profile.hpp>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

namespace PandaUI {
class Label;
}

namespace Editor {

// VIEW: the built-in profiler panel — the top scopes of the frame (Foundation::Profiler).
// Display only: the snapshot is pushed by EditorLayer via updateSnapshot (once per ~0.25 s).
// The rows are a fixed pool of labels (sorting and aggregation are done by the profiler,
// smoothing across frames is an EMA here).
class ProfilerPanelView final : public EditorView {
public:
    static constexpr size_t MAX_ROWS = 24;

    ProfilerPanelView();

    void updateSnapshot(); // Input: re-read Foundation::Profiler (called by EditorLayer)
    void copyAll();        // the current snapshot as text to the clipboard

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_frameLabel;
    struct Row {
        std::shared_ptr<PandaUI::Panel> container;
        std::shared_ptr<PandaUI::Label> name;
        std::shared_ptr<PandaUI::Label> value;
    };
    std::array<Row, MAX_ROWS> m_rows;
    std::unordered_map<const char *, float> m_emaTotals; // smoothed total per scope
    std::string m_clipboardText; // text of the last snapshot (for Copy)
};

} // namespace Editor
