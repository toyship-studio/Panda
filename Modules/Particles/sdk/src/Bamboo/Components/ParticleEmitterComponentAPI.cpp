#include "Bamboo/Components/ParticleEmitterComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/ParticlesTable.hpp"

namespace Bamboo::ParticleEmitterComponentAPI {

namespace {
    const Panda::Scripting::ParticlesHostApi *table(const char *apiName) {
        if (!Internal::requireMainThreadApi(apiName)) { return nullptr; }
        return Internal::particlesTable();
    }
} // namespace

void setPlaying(EntityHandle entity, bool playing) {
    if (const Panda::Scripting::ParticlesHostApi *api = table("ParticleEmitterComponentAPI::setPlaying")) {
        api->emitter_SetPlaying(entity.id, playing ? 1 : 0);
    }
}

bool isPlaying(EntityHandle entity) {
    const Panda::Scripting::ParticlesHostApi *api = table("ParticleEmitterComponentAPI::isPlaying");
    return api != nullptr && api->emitter_IsPlaying(entity.id) != 0;
}

int emit(EntityHandle entity, int count) {
    const Panda::Scripting::ParticlesHostApi *api = table("ParticleEmitterComponentAPI::emit");
    if (api == nullptr || count <= 0) { return 0; }
    return static_cast<int>(api->emitter_Emit(entity.id, static_cast<uint32_t>(count), nullptr, nullptr, nullptr));
}

int emitAt(EntityHandle entity, Vec3 position, int count, Vec4 tint, TextureRect rect) {
    const Panda::Scripting::ParticlesHostApi *api = table("ParticleEmitterComponentAPI::emitAt");
    if (api == nullptr || count <= 0) { return 0; }
    const float position3[3] = {position.x, position.y, position.z};
    const float tint4[4] = {tint.x, tint.y, tint.z, tint.w};
    const float rect4[4] = {rect.x, rect.y, rect.width, rect.height};
    return static_cast<int>(api->emitter_Emit(entity.id, static_cast<uint32_t>(count), position3, tint4, rect4));
}

} // namespace Bamboo::ParticleEmitterComponentAPI
