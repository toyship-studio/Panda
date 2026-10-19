#include "UI/Documents/SettingsDocuments.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>

namespace Editor {

/* ------------------------------ Project settings ------------------------------ */

ProjectSettingsDocument::ProjectSettingsDocument(Host host)
    : m_host(std::move(host))
    , m_view(std::make_shared<SettingsSectionsView>(Panda::SettingsScope::Project)) {
    m_view->setStore(&m_host.store());
}

std::string ProjectSettingsDocument::title() {
    return "Project Settings";
}

std::string ProjectSettingsDocument::icon() {
    return Ic::Configure;
}

std::shared_ptr<PandaUI::View> ProjectSettingsDocument::view() {
    return m_view;
}

bool ProjectSettingsDocument::isDirty() {
    return m_dirty;
}

bool ProjectSettingsDocument::save() {
    if (m_host.save && !m_host.save()) { return false; }
    m_dirty = false;
    return true;
}

void ProjectSettingsDocument::revert() {
    if (m_host.revert) { m_host.revert(); }
    m_history.CLEAR(); // the history described the data that is gone
    m_dirty = false;
    refresh();
}

Panda::CommandHistory *ProjectSettingsDocument::undoStack() {
    return &m_history;
}

void ProjectSettingsDocument::refresh() {
    m_view->rebuild();
}

void ProjectSettingsDocument::markDirty() {
    m_dirty = true;
}

void ProjectSettingsDocument::revealSection(const std::string &title) {
    m_view->revealSection(title);
}

/* ------------------------------ Editor settings ------------------------------ */

EditorSettingsDocument::EditorSettingsDocument(Panda::TypeContext &store)
    : m_view(std::make_shared<SettingsSectionsView>(Panda::SettingsScope::Editor)) {
    m_view->setStore(&store);
}

std::string EditorSettingsDocument::title() {
    return "Editor Settings";
}

std::string EditorSettingsDocument::icon() {
    return Ic::Configure;
}

std::shared_ptr<PandaUI::View> EditorSettingsDocument::view() {
    return m_view;
}

void EditorSettingsDocument::refresh() {
    m_view->rebuild();
}

void EditorSettingsDocument::revealSection(const std::string &title) {
    m_view->revealSection(title);
}

} // namespace Editor
