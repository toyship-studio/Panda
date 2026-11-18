#pragma once

#include <Panda/Assets/Base/Asset.hpp> // AssetId

#include <functional>

namespace Panda {

class EditorLayer;
class FileDocument;
class WorldFile;

// What Save, a reload from disk and everything that drops what is in memory do to the file documents of the
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

    // ---- Reload: file → memory. The user has no command for it: the editor takes what changes
    // ---- on disk by itself (takeDiskChanges), and an agent asks for it (reloadFromDisk).
    // The engine and the disk agree again, asked for (the `reload` tool of the MCP server): the
    // game stops, the asset registry follows the project tree, then a document changed on disk
    // is reread — by the time of its file — and the script build is loaded when it is another
    // one. Where the memory has edits too, the version that stays is asked for; Cancel aborts
    // the rest. A module switched on in project.json opens the project again.
    void reloadFromDisk();
    // The same, done by the editor itself when the disk changed under it (DiskWatch): nothing is
    // asked and nothing is interrupted. A document with unsaved edits keeps them — it stays
    // changed on disk, and saving it asks which version stays; a project whose module set has
    // grown is opened again by the disk watch, once nothing is unsaved. `newFiles` — a file came
    // or went: the asset registry follows the tree first. Not called while the game plays.
    void takeDiskChanges(bool newFiles);
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
    // The engine and the disk agree again. `asked` — somebody asked for it (reloadFromDisk): a
    // conflict is a question, a grown module set opens the project again, and the log says so
    // when there was nothing to take. `followTree` — the asset registry reads the tree first.
    void syncWithDisk(bool asked, bool followTree);
    void reload(FileDocument &file);                // file → memory, the tabs follow

    EditorLayer &m_app;
};

} // namespace Panda
