#pragma once

#include <Rain/Rain.hpp>

namespace Panda {

// "physics" of the editor scope (the user's settings.json): the viewport options of the module.
struct PhysicsEditorSettings final : public Rain::Codable {
    bool showColliders = true; // wireframe outlines of the colliders in the Scene tab
    bool showJoints = true;    // the anchors and axes of the joints, and a line to the connected body

    RAIN_FIELDS_BEGIN(PhysicsEditorSettings)
    RAIN_FIELD(showColliders)
    RAIN_FIELD(showJoints)
    RAIN_FIELDS_END
};

} // namespace Panda
