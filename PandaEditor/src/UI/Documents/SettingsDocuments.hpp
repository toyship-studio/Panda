#pragma once

#include "UI/Panels/SettingsSectionsView.hpp"

#include <PandaEditorKit/Document.hpp>

#include <Panda/Base/TypeContext.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Editor {

// The settings documents (the document model, context/plugins.md): the sections of a scope in a
// tab. The project settings are a document of `.Panda/project.json` — their own undo stack, dirty
// until saved, the close question; the editor settings apply at once and are saved by every edit —
// no dirty state and no undo. The world settings are not a document: they are the sidebar of the
// viewport, inside the world document.

class ProjectSettingsDocument final : public Document {
public:
    struct Host {
        std::function<Panda::TypeContext &()> store;
        std::function<bool()> save;   // writes project.json; false — the write failed
        std::function<void()> revert; // rereads project.json
    };
    explicit ProjectSettingsDocument(Host host);

    std::string title() override;
    std::string icon() override;
    std::shared_ptr<PandaUI::View> view() override;
    bool isDirty() override;
    bool save() override;
    void revert() override;
    Panda::CommandHistory *undoStack() override;
    void refresh() override;

    void markDirty(); // an edit went into the store (EditorHost::commitSettings)
    void revealSection(const std::string &title);

private:
    Host m_host;
    bool m_dirty = false;
    Panda::CommandHistory m_history;
    std::shared_ptr<SettingsSectionsView> m_view;
};

class EditorSettingsDocument final : public Document {
public:
    explicit EditorSettingsDocument(Panda::TypeContext &store);

    std::string title() override;
    std::string icon() override;
    std::shared_ptr<PandaUI::View> view() override;
    void refresh() override;

    void revealSection(const std::string &title);

private:
    std::shared_ptr<SettingsSectionsView> m_view;
};

} // namespace Editor
