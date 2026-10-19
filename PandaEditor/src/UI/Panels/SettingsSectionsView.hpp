#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <Panda/Base/TypeContext.hpp>
#include <Panda/Extensions/Settings.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Editor {

// VIEW: the sections of one settings scope (EditorExtensions::settingsSections, the core's and the
// modules') over a store, in registration order, in a vertical scroll. The world sidebar of the
// viewport, the project and the editor settings documents and the Display popover of the viewport
// are built from it. rebuild rereads the store (a world change, undo/redo, a reload); a section
// keeps its expanded state across rebuilds.
class SettingsSectionsView final : public EditorView {
public:
    explicit SettingsSectionsView(Panda::SettingsScope scope);

    // The store the sections edit; nullptr — no sections (no world).
    void setStore(Panda::TypeContext *store);
    void rebuild();
    // Expands the section with this title and scrolls the list to it (the "Settings" link of a
    // material field).
    void revealSection(const std::string &title);
    // Only the sections with these titles (empty — all): the Display popover shows the viewport
    // section alone.
    void setTitleFilter(std::function<bool(const std::string &)> filter);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    Panda::SettingsScope m_scope;
    Panda::TypeContext *m_store = nullptr;
    std::function<bool(const std::string &)> m_filter;
    std::shared_ptr<PandaUI::ScrollView> m_body;
    std::shared_ptr<PandaUI::Panel> m_sections;
    std::unordered_map<std::string, std::shared_ptr<PandaUI::CollapsibleSection>> m_byTitle;
    std::unordered_map<std::string, bool> m_expanded; // the expanded state survives a rebuild
};

} // namespace Editor
