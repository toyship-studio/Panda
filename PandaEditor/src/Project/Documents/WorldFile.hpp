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
    FileDocuments &m_owner;
    uint32_t m_id;
    World m_world;
    path_t m_path;
    DiskStamp m_stamp;
    EditorCameraSettings m_camera;
    bool m_hasCamera = false; // false — the world was not shown yet: the camera stays where it is
};

} // namespace Panda
