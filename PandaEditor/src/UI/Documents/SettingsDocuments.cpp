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
    return m_host.isDirty && m_host.isDirty();
}

bool ProjectSettingsDocument::save() {
    return !m_host.save || m_host.save();
}

void ProjectSettingsDocument::revert() {
    if (m_host.revert) { m_host.revert(); }
    fileReloaded();
}

std::filesystem::path ProjectSettingsDocument::filePath() {
    return m_host.filePath ? m_host.filePath() : std::filesystem::path();
}

void ProjectSettingsDocument::fileReloaded() {
    m_history.CLEAR(); // the history described the data that is gone
    refresh();
}

Panda::CommandHistory *ProjectSettingsDocument::undoStack() {
    return &m_history;
}

void ProjectSettingsDocument::refresh() {
    m_view->rebuild();
}

void ProjectSettingsDocument::markDirty() {
    if (m_host.markDirty) { m_host.markDirty(); }
}

void ProjectSettingsDocument::revealSection(const std::string &title) {
    m_view->revealSection(title);
}

/* ------------------------------ Editor settings ------------------------------ */

EditorSettingsDocument::EditorSettingsDocument(
    Panda::TypeContext &store, std::function<std::filesystem::path()> filePath
)
    : m_view(std::make_shared<SettingsSectionsView>(Panda::SettingsScope::Editor))
    , m_filePath(std::move(filePath)) {
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

std::filesystem::path EditorSettingsDocument::filePath() {
    return m_filePath ? m_filePath() : std::filesystem::path();
}

} // namespace Editor
