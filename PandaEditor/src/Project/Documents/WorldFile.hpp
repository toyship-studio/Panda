#pragma once

#include "Project/Documents/FileDocument.hpp"
#include "Project/Settings/ProjectSettings.hpp" // EditorCameraSettings

#include <Panda/GameLogic/World.hpp>

#include <cstdint>
#include <string>

namespace Panda {

class FileDocuments;

// A world and its `.world` file. The editor keeps any number of worlds open, a tab each, one of
// them active (FileDocuments). The document owns the world: closing the document drops it.
//
// A prefab is a document of the same kind (context/prefabs.md): its `.prefab` file holds one
// tree of entities, and the document holds them in a world of their own — edited like any
// world, never played. What tells the two apart is the file (isPrefab).
class WorldFile final : public FileDocument {
public:
    WorldFile(FileDocuments &owner, uint32_t id);

    // Stable while the world is open: the key of its tab.
    uint32_t id() const {
        return m_id;
    }
    World &world() {
        return m_world;
    }
    // The file as a tab shows it; "Untitled World" for a world without one.
    std::string name() const;
    // The document of a prefab: the world holds the entities of its file, under one root.
    static bool isPrefabFile(const path_t &path) {
        return path.extension() == ".prefab";
    }
    bool isPrefab() const {
        return isPrefabFile(m_path);
    }
    // The root of the prefab: the entity its file starts with. Everything of the prefab stands
    // under it, and it is what an instance is made of. Invalid — the document is a world.
    Entity prefabRoot();

    std::string title() const override;
    path_t path() const override {
        return m_path;
    }
    bool isDirty() const override {
        return m_world.isChanged();
    }
    bool isChangedOnDisk() const override {
        return m_stamp.isStale(m_path);
    }
    bool save() override;   // the file; a world without one asks for it (Save As)
    bool reload() override; // the same file again

    // The file side: the world from a file, the world into a file. The path and the disk stamp
    // follow. A file that cannot be read leaves the world and its path as they were.
    bool read(const path_t &path);
    bool write(const path_t &path);
    // The text the file of the document would get now (a prefab: the tree of its root). false —
    // there is nothing to write (a prefab without its root).
    bool text(std::string &text);
    // The unsaved edits of another session come back (DocumentBackups): the world from the copy
    // of its text, as edits of the file at `path` (empty — a world without a file) made over the
    // file as it was at `stamp` — so a file changed on disk since then is asked about at Save.
    bool restore(const path_t &copy, const path_t &path, int64_t stamp);
    const DiskStamp &stamp() const {
        return m_stamp;
    }
    // How a question names the document of a file: `world "Forest"`, `prefab "Tree"`.
    static std::string titleOf(const path_t &path);
    // The file was moved or renamed (the Content Browser): the document follows it, unsaved edits
    // and all — it is the same file under another name, so the stamp is of the new path.
    void fileMoved(const path_t &path) {
        m_path = path;
        m_stamp.sync(path);
    }

    // Where the editor camera stood in this world: the camera stays with the world it was looking
    // at, and comes back with it (the tab, the next session).
    bool hasCamera() const {
        return m_hasCamera;
    }
    const EditorCameraSettings &camera() const {
        return m_camera;
    }
    void setCamera(const EditorCameraSettings &camera) {
        m_camera = camera;
        m_hasCamera = true;
    }

private:
    // The world from the text of a file; `path` is the file of the document from now on, `source`
    // is what the messages name. A text that is not a world leaves everything as it was.
    bool load(const std::string &text, const path_t &path, const path_t &source);

    FileDocuments &m_owner;
    uint32_t m_id;
    World m_world;
    path_t m_path;
    DiskStamp m_stamp;
    UUID m_prefabRoot = 0; // of a prefab: its root entity
    EditorCameraSettings m_camera;
    bool m_hasCamera = false; // false — the world was not shown yet: the camera stays where it is
};

} // namespace Panda
