#include "App/DocumentActions.hpp"

#include "App/EditorLayer.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/EditorController.hpp"

#include <Foundation/Logger.hpp>

#include <cctype>
#include <string>
#include <vector>

namespace Panda {

namespace {

    // A document title at the start of a sentence: `world "Forest"` → `World "Forest"`.
    std::string sentenceStart(std::string title) {
        if (!title.empty()) { title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0]))); }
        return title;
    }

} // namespace

DocumentActions::DocumentActions(EditorLayer &app)
    : m_app(app) {}

/* ------------------------------ Save ------------------------------ */

bool DocumentActions::save(FileDocument &file) {
    // A file changed on disk since the editor read it is not overwritten in silence.
    if (file.isChangedOnDisk()) { return resolveConflict(file) != Resolution::Cancelled; }
    if (!file.save()) { return false; }
    LOG_INFO_EDITOR("Saved %s.", file.title().c_str());
    if (&file == m_app.documents().activeWorld() && m_app.isPlaying()) {
        // Save writes the world being edited. Play runs on its copy: what the simulation and the
        // inspector change there lives until Stop.
        LOG_INFO_EDITOR("The changes made while playing are not saved: Stop discards them.");
    }
    return true;
}

bool DocumentActions::saveAll() {
    const FileDocumentList files = m_app.documents().list();
    for (FileDocument *file : files.all) {
        if (!file->isDirty()) { continue; }
        if (!save(*file)) { return false; }
    }
    return true;
}

bool DocumentActions::saveWorld(WorldFile &world) {
    // A world without a file yet is saved even when untouched: Save names it (Save As).
    if (!world.isDirty() && !world.path().empty()) { return true; }
    return save(world);
}

bool DocumentActions::saveAsset(AssetId asset) {
    const std::unique_ptr<AssetFile> file = m_app.documents().assetFile(asset);
    return file && save(*file);
}

/* ------------------------------ Reload ------------------------------ */

DocumentActions::Resolution DocumentActions::resolveConflict(FileDocument &file) {
    // The memory has edits and the file is to be replaced or was replaced: one of the two
    // versions stays, the user says which.
    const std::string title = sentenceStart(file.title()) +
                              (file.isChangedOnDisk() ? " was changed on disk and has unsaved changes"
                                                      : " has unsaved changes");
    const SystemTools::SaveChangesChoice choice = SystemTools::showSaveChangesDialog(
        title.c_str(), "Save them over the file on disk, or discard them and take the version on disk?"
    );
    if (choice == SystemTools::SaveChangesChoice::Cancel) { return Resolution::Cancelled; }
    if (choice == SystemTools::SaveChangesChoice::Save) {
        return file.save() ? Resolution::Saved : Resolution::Cancelled;
    }
    reload(file);
    return Resolution::Reloaded;
}

void DocumentActions::reload(FileDocument &file) {
    // What follows a read is the document's to report (documentsDidReadSettings, documentsDidLoadWorld);
    // the forms of asset and entity tabs follow the live objects themselves.
    if (!file.reload()) { LOG_ERROR_EDITOR("Reload: %s could not be read from disk", file.title().c_str()); }
}

DocumentActions::Resolution DocumentActions::takeDiskVersion(FileDocument &file) {
    if (file.isDirty()) { return resolveConflict(file); }
    reload(file);
    return Resolution::Reloaded;
}

int DocumentActions::revertUnsaved() {
    m_app.stop();
    int reverted = 0;
    const FileDocumentList files = m_app.documents().list();
    for (FileDocument *file : files.all) {
        if (!file->isDirty() || file->path().empty()) { continue; } // a new world has nothing to go back to
        LOG_INFO_EDITOR("Reverted %s.", file->title().c_str());
        reload(*file);
        ++reverted;
    }
    return reverted;
}

void DocumentActions::reloadFromDisk() {
    // Reload stops the game: what it reads may be what the game runs on.
    m_app.stop();
    syncWithDisk(true, true);
}

void DocumentActions::takeDiskChanges(bool newFiles) {
    if (m_app.isPlaying()) { return; }
    syncWithDisk(false, newFiles);
}

void DocumentActions::syncWithDisk(bool asked, bool followTree) {
    // A single synchronization of the editor with the disk: the asset registry follows the
    // project tree, then, by modification time, a file document changed on disk is reread; when
    // the memory has edits too, the user picks the version that stays (Cancel aborts the rest) —
    // or, when nobody asked, the document is left as it is. The script build is a step of its
    // own before the worlds (nothing edits it in memory).
    FileDocuments &documents = m_app.documents();
    ProjectLoader &loader = m_app.project();
    // The registry follows the tree first: a file put into the project since the last scan
    // becomes an asset (its sidecar gets an id), a file that is gone leaves the registry. The ids
    // of the known assets live in their sidecars and do not change, so nothing loaded is touched.
    if (auto *assetHandler = AssetHandlerEditor::current(); assetHandler != nullptr && followTree) {
        assetHandler->rescanProject();
        if (m_app.controller()) { m_app.controller()->refreshAssetPanels(); }
    }
    // What was taken, for the log: the first few by name, the rest by their number (a checkout
    // of a branch changes hundreds of files).
    constexpr int MOST_NAMED = 6;
    std::string reloaded;
    int reloadedCount = 0;
    const auto took = [&](const std::string &title) {
        if (++reloadedCount <= MOST_NAMED) { reloaded += (reloaded.empty() ? "" : ", ") + title; }
    };
    bool scriptsChecked = false;
    const auto reloadScripts = [&] {
        scriptsChecked = true;
        if (!loader.isScriptsChangedOnDisk()) { return; }
        loader.reloadScriptsDll();
        took("scripts");
    };
    const FileDocumentList files = documents.list();
    for (FileDocument *file : files.all) {
        if (!scriptsChecked && documents.isWorldFile(file)) { reloadScripts(); }
        if (!file->isChangedOnDisk()) { continue; }
        // Unsaved edits are the user's: without a question they stay, and so does the conflict.
        if (!asked && file->isDirty()) { continue; }
        const std::string title = file->title(); // the document may not outlive its reload
        const Resolution resolution = takeDiskVersion(*file);
        if (resolution == Resolution::Cancelled) { return; }
        if (resolution != Resolution::Reloaded) { continue; }
        took(title);
        // A module switched on in project.json opens the project again. Asked for, that happens
        // here, with the question about what is unsaved; when the editor takes the file by
        // itself, the disk watch does it once nothing is unsaved.
        if (asked && file == &documents.projectSettingsFile() && m_app.reopenProjectForModules()) { return; }
    }
    if (!scriptsChecked && loader.hasOpenedProject()) { reloadScripts(); }
    if (reloadedCount > MOST_NAMED) { reloaded += " and " + std::to_string(reloadedCount - MOST_NAMED) + " more"; }
    if (!reloaded.empty()) {
        LOG_INFO_EDITOR("Reload: %s", reloaded.c_str());
    } else if (asked) {
        LOG_INFO_EDITOR("Reload: everything is up to date");
    }
}

void DocumentActions::reloadWorldAsset(AssetId worldAsset) {
    // A broken file only logs an error.
    auto *assetHandler = AssetHandlerEditor::current();
    if (!assetHandler || !assetHandler->hasAsset(worldAsset)) { return; }
    const path_t assetRelativePath = assetHandler->assetPath(worldAsset);
    if (assetRelativePath.empty()) { return; }
    WorldFile *world = m_app.documents().findWorld(assetHandler->getProjectPath() / assetRelativePath);
    if (world == nullptr) { return; }
    if (world == m_app.documents().activeWorld()) { m_app.stop(); }
    takeDiskVersion(*world);
}

/* ------------------------------ The unsaved-changes question ------------------------------ */

void DocumentActions::runAfterUnsavedQuestion(std::function<void()> action) {
    m_app.stop();
    FileDocuments &documents = m_app.documents();
    std::vector<std::string> dirty;
    {
        const FileDocumentList files = documents.list();
        for (FileDocument *file : files.all) {
            if (file->isDirty()) { dirty.push_back(file->title()); }
        }
    }
    if (dirty.empty()) {
        if (action) { action(); }
        return;
    }
    // The question names WHAT is unsaved: up to three documents by name, more — by count.
    std::string title = "Save ";
    if (dirty.size() > 3) {
        title += std::to_string(dirty.size()) + " modified files";
    } else {
        for (size_t i = 0; i < dirty.size(); i++) {
            if (i > 0) { title += i + 1 == dirty.size() ? " and " : ", "; }
            title += dirty[i];
        }
    }
    title += "?";
    const SystemTools::SaveChangesChoice choice =
        SystemTools::showSaveChangesDialog(title.c_str(), "Your changes will be lost otherwise.");
    if (choice == SystemTools::SaveChangesChoice::Cancel) { return; }
    if (choice == SystemTools::SaveChangesChoice::Save) {
        if (!saveAll()) { return; } // Save As cancelled → stop
    } else {
        // Don't Save: the action drops the world and the assets with their edits. The project
        // scope outlives it and is written again with the session (closing, switching), so its
        // edits go back to the file here — a declined edit must not reach the disk later.
        if (documents.projectSettingsFile().isDirty()) { reload(documents.projectSettingsFile()); }
    }
    if (action) { action(); }
}

} // namespace Panda
