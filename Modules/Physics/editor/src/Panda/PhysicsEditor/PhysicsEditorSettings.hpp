#pragma once

#include <Rain/Rain.hpp>

namespace Panda {

// "physics" of the editor scope (the user's settings.json): the viewport options of the module.
struct PhysicsEditorSettings final : public Rain::Codable {
    bool showColliders = true; // wireframe outlines of the colliders in EDIT and SIMULATE

    RAIN_FIELDS_BEGIN(PhysicsEditorSettings)
    RAIN_FIELD(showColliders)
    RAIN_FIELDS_END
};

} // namespace Panda
