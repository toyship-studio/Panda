#pragma once

#include <Panda/Assets/Base/Asset.hpp> // AssetId

#include <functional>

namespace Panda {

class EditorLayer;
class FileDocument;
class WorldFile;

// What Save, Reload and everything that drops what is in memory do to the file documents of the
// editor (FileDocuments): the writes, the reads, and the question to the user where the memory
// and the disk disagree or edits would be lost. Written once over FileDocument, whatever the
// document is — a world, an asset, a settings file (context/architecture.md, "The document
// model").
class DocumentActions final {
public:
    explicit DocumentActions(EditorLayer &app);

    // ---- Save: memory → file. false — cancelled or failed.
    bool save(FileDocument &file);
    bool saveAll(); // every document with unsaved edits
    bool saveWorld(WorldFile &world);
    bool saveAsset(AssetId asset);

    // ---- Reload: file → memory.
    // The engine and the disk agree again (the Reload button, Cmd+R): the asset registry follows
    // the project tree, then a document changed on disk is reread — by the time of its file — and
    // the script build is loaded when it is newer. Where the memory has edits too, the user picks
    // the version that stays; Cancel aborts the rest.
    void reloadFromDisk();
    // The unsaved edits of every document that has a file are dropped: the memory is what the
    // disk holds again. Returns the number of documents read.
    int revertUnsaved();
    // Reimport of a world in the Content Browser: reread when it is open (unsaved edits ask which
    // version stays). A world that is not open has nothing in memory to refresh.
    void reloadWorldAsset(AssetId worldAsset);

    // ---- The gate of everything that drops what is in memory — another project, closing,
    // ---- quitting: the unsaved documents are named and saved or discarded, then the action
    // ---- runs; Cancel leaves everything as it is.
    void runAfterUnsavedQuestion(std::function<void()> action);

private:
    enum class Resolution { Saved, Reloaded, Cancelled };
    // The memory has edits and so does the disk (or the disk version is asked for): the user
    // picks the version that stays.
    Resolution resolveConflict(FileDocument &file);
    Resolution takeDiskVersion(FileDocument &file); // file → memory, with the question when dirty
    void reload(FileDocument &file);                // file → memory, the tabs follow

    EditorLayer &m_app;
};

} // namespace Panda
