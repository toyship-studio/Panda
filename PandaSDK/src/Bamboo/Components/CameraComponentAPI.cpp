#include "Bamboo/Components/CameraComponentAPI.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace CameraComponentAPI {
    void setPostprocessingEnabled(EntityHandle entity, bool enabled) {
        Panda::ExternalCalls::cameraComponent_SetPostprocessingEnabled(entity.id, enabled ? 1u : 0u);
    }

    bool isPostprocessingEnabled(EntityHandle entity) {
        uint32_t enabled = 0;
        Panda::ExternalCalls::cameraComponent_GetPostprocessingEnabled(entity.id, &enabled);
        return enabled != 0;
    }

    namespace {
        void getBloom(EntityHandle entity, uint32_t &enabled, float &intensity, float &radius) {
            Panda::ExternalCalls::cameraComponent_GetBloom(entity.id, &enabled, &intensity, &radius);
        }
    } // namespace

    void setBloomEnabled(EntityHandle entity, bool enabled) {
        uint32_t wasEnabled = 0;
        float intensity = 0.f, radius = 0.f;
        getBloom(entity, wasEnabled, intensity, radius);
        Panda::ExternalCalls::cameraComponent_SetBloom(entity.id, enabled ? 1u : 0u, intensity, radius);
    }

    bool isBloomEnabled(EntityHandle entity) {
        uint32_t enabled = 0;
        float intensity = 0.f, radius = 0.f;
        getBloom(entity, enabled, intensity, radius);
        return enabled != 0;
    }

    void setBloomIntensity(EntityHandle entity, float value) {
        uint32_t enabled = 0;
        float intensity = 0.f, radius = 0.f;
        getBloom(entity, enabled, intensity, radius);
        Panda::ExternalCalls::cameraComponent_SetBloom(entity.id, enabled, value, radius);
    }

    float getBloomIntensity(EntityHandle entity) {
        uint32_t enabled = 0;
        float intensity = 0.f, radius = 0.f;
        getBloom(entity, enabled, intensity, radius);
        return intensity;
    }

    void setBloomRadius(EntityHandle entity, float value) {
        uint32_t enabled = 0;
        float intensity = 0.f, radius = 0.f;
        getBloom(entity, enabled, intensity, radius);
        Panda::ExternalCalls::cameraComponent_SetBloom(entity.id, enabled, intensity, value);
    }

    float getBloomRadius(EntityHandle entity) {
        uint32_t enabled = 0;
        float intensity = 0.f, radius = 0.f;
        getBloom(entity, enabled, intensity, radius);
        return radius;
    }
} // namespace CameraComponentAPI

} // namespace Bamboo
