#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

namespace CameraComponentAPI {
    // Toggle the camera postprocessing chain at runtime (e.g. pause menu blur on/off).
    void setPostprocessingEnabled(EntityHandle entity, bool enabled);
    bool isPostprocessingEnabled(EntityHandle entity);
    // Built-in camera bloom (engine pyramid; glow sources are material emissive).
    void setBloomEnabled(EntityHandle entity, bool enabled);
    bool isBloomEnabled(EntityHandle entity);
    void setBloomIntensity(EntityHandle entity, float intensity);
    float getBloomIntensity(EntityHandle entity);
    void setBloomRadius(EntityHandle entity, float radius);
    float getBloomRadius(EntityHandle entity);
}; // namespace CameraComponentAPI

} // namespace Bamboo
