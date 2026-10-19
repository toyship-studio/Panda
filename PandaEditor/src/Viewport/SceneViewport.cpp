#include "SceneViewport.hpp"
#include "Foundation/Logger.hpp"

#include <Panda.hpp>
#include <Fern/Fern.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/SelectionContext.hpp>

namespace Panda {

bool SceneViewport::RectSelection::isEqualTo(IRect _rect) {
    return rect.size.width == _rect.size.width && rect.size.height == _rect.size.height;
}

SceneViewport::SceneViewport(CameraController *cameraController)
    : m_viewport()
    , m_rectSelection()
    , m_camera(nullptr)
    , m_cameraController(cameraController)
    , m_focused(false)
    , m_hovered(false)
    , m_focusNextFrame(true)
    , m_gizmos(nullptr) {}

SceneViewport::~SceneViewport() {}

void SceneViewport::initWithSize(Vec2 size) {
    m_viewport.initWithSize(size);
}

void SceneViewport::updateViewportSize(Size size) {
    if (size.width < 1 || size.height < 1) { return; }
    if (m_camera) { m_camera->setViewportSize(size); }
    m_viewport.updateSize(size);
}

void SceneViewport::drawOutline(float dt) {
    std::unordered_set<UUID> selection = SelectionContext::getSelectedEntities();
    m_viewport.drawOutline(dt, selection);
}

std::unordered_set<UUID> SceneViewport::getEntitiesFromRequest(const SceneViewport::ReadFrameBufferRequest &request) {
    std::unordered_set<UUID> ids;
    Fern::Size dpi = Application::get()->getMainWindow()->getDpi();
    for (int regX = 0; regX < request.bufferSize.width; regX++) {
        for (int regY = 0; regY < request.bufferSize.height; regY++) {
            uint32_t texelIndex = request.bufferSize.width * regY + regX;
            uint32_t *buffer = static_cast<uint32_t *>(request.mem.data);
            uint32_t hoveredId = buffer[texelIndex];
            if (hoveredId != 0) { ids.insert(hoveredId); }
        }
    }
    return ids;
}

void SceneViewport::readIdsMemoryIfNeed() {
    if (m_rectSelection.readRequests.empty()) { return; }
    std::unordered_set<UUID> ids;
    auto &requests = m_rectSelection.readRequests;
    auto it = requests.begin();
    bool hasRequests = false;
    while (it != requests.end()) {
        ReadFrameBufferRequest &request = *it;
        if (Miren::getFrameNumber() >= request.ready) {
            std::unordered_set<UUID> _ids = getEntitiesFromRequest(request);
            ids.insert(_ids.begin(), _ids.end());
            request.mem.release();
            it = requests.erase(it);
            hasRequests = true;
        } else {
            it++;
        }
    }
    if (!hasRequests) { return; }
    std::unordered_set<UUID> select;
    std::unordered_set<UUID> unselect;
    // ctrl or shift was held down
    if (m_rectSelection.appendSelection) {
        // Add to unselect those that are in m_rectSelection.currentSelection and not in ids.
        // That is, they have left the region of the current selection. Deselect them
        std::unordered_set<UUID> currentIds = m_rectSelection.currentSelection;
        for (UUID currentlySelected : currentIds) {
            if (!ids.contains(currentlySelected) && !m_rectSelection.initialSelection.contains(currentlySelected)) {
                unselect.insert(currentlySelected);
                m_rectSelection.currentSelection.erase(currentlySelected);
            }
        }
        // Add to select those that are in m_rectSelection.initialSelection and not in ids.
        for (UUID initiallySelected : m_rectSelection.initialSelection) {
            if (!ids.contains(initiallySelected) && !m_rectSelection.currentSelection.contains(initiallySelected)) {
                select.insert(initiallySelected);
                m_rectSelection.currentSelection.insert(initiallySelected);
            }
        }
        // Select those that fall within the region
        for (UUID id : ids) {
            bool selectedBefore = m_rectSelection.initialSelection.contains(id);
            bool selectedCurrently = m_rectSelection.currentSelection.contains(id);
            // If it was selected, apply the inversion. It must be deselected
            if (selectedBefore && selectedCurrently) {
                unselect.insert(id);
                m_rectSelection.currentSelection.erase(id);
                // If it was not selected, select it. Selection inversion within the region
            } else if (!selectedBefore && !selectedCurrently) {
                select.insert(id);
                m_rectSelection.currentSelection.insert(id);
            }
        }
    } else {
        // Select those that fall within the region
        for (UUID id : ids) {
            if (!m_rectSelection.currentSelection.contains(id)) {
                select.insert(id);
                m_rectSelection.currentSelection.insert(id);
            }
        }
        std::unordered_set<UUID> currentIds = m_rectSelection.currentSelection;
        // Remove those that did not fall within it
        for (UUID id : currentIds) {
            if (!ids.contains(id)) {
                unselect.insert(id);
                m_rectSelection.currentSelection.erase(id);
            }
        }
    }
    SelectionContext::addSelectedEntities(select);
    SelectionContext::removeSelectedEntities(unselect);
    if (!m_rectSelection.appendSelection) { SelectionContext::unselectAllFiles(); }
}

void SceneViewport::beginRectSelection(IVec2 viewportPoint, bool append) {
    m_rectSelection.isStarted = true;
    m_rectSelection.appendSelection = append;
    m_rectSelection.rect = IRect(viewportPoint.x, viewportPoint.y, 0, 0);
    m_rectSelection.currentSelection = SelectionContext::getSelectedEntities();
    m_rectSelection.initialSelection = m_rectSelection.currentSelection;
}

void SceneViewport::updateRectSelection(IVec2 viewportPoint) {
    IRect selectedRegion = m_rectSelection.rect;
    selectedRegion.size.width = viewportPoint.x - m_rectSelection.rect.origin.x;
    selectedRegion.size.height = viewportPoint.y - m_rectSelection.rect.origin.y;
    if (selectedRegion.size.width == 0) { selectedRegion.size.width = 1; }
    if (selectedRegion.size.height == 0) { selectedRegion.size.height = 1; }
    if (m_rectSelection.isEqualTo(selectedRegion)) { return; }
    m_rectSelection.rect = selectedRegion;
    ReadFrameBufferRequest &request = m_rectSelection.readRequests.emplace_back();
    if (selectedRegion.size.width < 0) {
        selectedRegion.origin.x += selectedRegion.size.width;
        selectedRegion.size.width = -selectedRegion.size.width;
    }
    if (selectedRegion.size.height < 0) {
        selectedRegion.origin.y += selectedRegion.size.height;
        selectedRegion.size.height = -selectedRegion.size.height;
    }
    Fern::Size dpi = Application::get()->getMainWindow()->getDpi();
    selectedRegion.size.x *= dpi.x;
    selectedRegion.size.y *= dpi.y;
    selectedRegion.origin.x *= dpi.x;
    selectedRegion.origin.y *= dpi.y;
    request.bufferSize = selectedRegion.size;
    uint32_t bufferSize =
        sizeof(uint32_t) * selectedRegion.size.width * dpi.width * selectedRegion.size.height * dpi.height;
    request.mem = Foundation::Memory::alloc(bufferSize);
    memset(request.mem.data, 0, bufferSize);
    uint32_t textureHeight = m_viewport.getFrame().size.height * dpi.y;
    request.ready = Miren::readFrameBuffer(
        m_viewport.getIdsFrameBuffer(),
        0,
        selectedRegion.origin.x,
        // Since the texture is stored flipped, the Y coordinate is flipped here
        textureHeight - selectedRegion.origin.y - selectedRegion.size.height,
        selectedRegion.size.width,
        selectedRegion.size.height,
        request.mem.data
    );
}

void SceneViewport::endRectSelection() {
    m_rectSelection.isStarted = false;
    m_rectSelection.rect = IRect(0, 0, 0, 0);
}

void SceneViewport::cancelRectSelection() {
    endRectSelection();
    m_rectSelection.appendSelection = false;
    m_rectSelection.initialSelection.clear();
    m_rectSelection.currentSelection.clear();
    for (ReadFrameBufferRequest &request : m_rectSelection.readRequests) {
        request.mem.release();
    }
    m_rectSelection.readRequests.clear();
}

void SceneViewport::unselectAll() {
    SelectionContext::unselectAll();
}

void SceneViewport::setCamera(Camera *camera) {
    m_camera = camera;
    m_gizmos.setCamera(camera);
    if (m_camera) { m_camera->setViewportSize(m_viewport.getFrame().size); }
}

void SceneViewport::setWorld(World *world) {
    m_gizmos.setWorld(world);
}

bool SceneViewport::isFocused() {
    return m_focused;
}

bool SceneViewport::isHovered() {
    return m_hovered;
}

Rect SceneViewport::getFrame() {
    return m_viewport.getFrame();
}

void SceneViewport::syncExternalFrame(Rect frame) {
    if (m_viewport.getFrame().size != frame.size) { updateViewportSize(frame.size); }
    m_viewport.updateOrigin(frame.origin);
    Input::setViewportFrame(m_viewport.getFrame());
}

void SceneViewport::driveRectSelection(IVec2 startPoint, IVec2 currentPoint, bool additive, bool committed) {
    // A no-move click ends up a 1×1 rect = single-entity pick. Same machinery queues async IDS reads and
    // updates SelectionContext (incl. deselecting files on a plain entity click).
    if (!m_rectSelection.isStarted) { beginRectSelection(startPoint, additive); }
    updateRectSelection(currentPoint);
    if (committed) { endRectSelection(); }
}

void SceneViewport::pollSelection() {
    readIdsMemoryIfNeed(); // apply async IDS reads queued by driveRectSelection (call each frame)
}

void SceneViewport::focus() {
    m_focusNextFrame = true;
}

} // namespace Panda
