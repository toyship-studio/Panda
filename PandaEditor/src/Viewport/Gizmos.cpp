//
// Created by Michael Andreichev on 06.10.2024.
//

#include "Gizmos.hpp"

#include "Viewport/Gizmo/Gizmo.hpp"
#include <Panda/WorldCommands/Impl/EntityTransformCommand.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Panda {

namespace {

    Gizmo::Operation toGizmoOperation(Gizmos::Operation operation) {
        switch (operation) {
            case Gizmos::Operation::Rotate:
                return Gizmo::rotate;
            case Gizmos::Operation::Scale:
                return Gizmo::scale;
            case Gizmos::Operation::Translate:
            default:
                return Gizmo::translate;
        }
    }

    Gizmo::Mode toGizmoMode(Gizmos::Space space) {
        switch (space) {
            case Gizmos::Space::World:
                return Gizmo::world;
            case Gizmos::Space::Local:
            default:
                return Gizmo::local;
        }
    }

} // namespace

Gizmos::Gizmos(Camera *camera)
    : m_camera(camera)
    , m_world(nullptr)
    , m_operation(Operation::Translate)
    , m_space(Space::Local)
    , m_overlayHovered(false)
    , m_gizmoVisible(false) {}

bool Gizmos::manipulate(Rect viewportRect, const Gizmo::InputState &input, const glm::mat4 *viewMatrix) {
    Gizmo::setInput(input);
    Gizmo::beginFrame();
    m_gizmoVisible = false;
    if (!viewMatrix || !m_camera || !m_world || SelectionContext::isEmpty()) { return false; }
    m_gizmoVisible = true;
    Gizmo::setOrthographic(false);
    Gizmo::setRect(viewportRect.origin.x, viewportRect.origin.y, viewportRect.size.width, viewportRect.size.height);

    glm::mat4 view = *viewMatrix;
    glm::mat4 projection = m_camera->getProjection();
    glm::mat4 transform = SelectionContext::getMedianMatrix();
    glm::mat4 delta = glm::mat4(1.0f);
    const bool manipulated = Gizmo::manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        toGizmoOperation(m_operation),
        toGizmoMode(m_space),
        glm::value_ptr(transform),
        glm::value_ptr(delta)
    );
    if (!manipulated) { return true; } // visible but not being moved — draw it

    auto ids = SelectionContext::getManipulatingEntities();
    std::unordered_set<Entity> entities = m_world->getById(ids);
    EntityTransformCommand transformCommand(entities);
    transformCommand.saveBeforeEdit();
    for (auto entity : entities) {
        TransformComponent &transformComponent = entity.getTransform();
        glm::mat4 newWorldTransform = delta * m_world->getWorldSpaceTransformMatrix(entity);
        transformComponent.setTransform(newWorldTransform);
        m_world->convertToLocalSpace(entity);
        if (m_operation == Operation::Scale) {
            entity.scaleUpdated(); // the systems rebuild what depends on the scale (collider shapes)
        } else {
            entity.transformUpdated(); // a teleport of the runtime state (bodies; the joints survive)
        }
    }
    transformCommand.saveAfterEdit();
    CommandHistory &cmd = m_world->getCommandHistory();
    cmd.SAVE(transformCommand, false);
    SelectionContext::updateValues();
    m_world->setChanged();
    return true;
}

bool Gizmos::isUsing() {
    return m_gizmoVisible && Gizmo::isUsing();
}

bool Gizmos::isHovered() const {
    return m_overlayHovered || (m_gizmoVisible && (Gizmo::isUsing() || Gizmo::isOver()));
}

} // namespace Panda
