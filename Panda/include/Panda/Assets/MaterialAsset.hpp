#pragma once

#include "Panda/Base/Base.hpp"
#include <vector>
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Assets/ShaderAsset.hpp"
#include "Panda/Renderer/MaterialData.hpp"
#include "Panda/Renderer/Std140Buffer.hpp"
#include "Base/AssetRef.hpp"

#include <Miren/Miren.hpp>
#include <cstdlib>

namespace Panda {

class MaterialAsset : public Asset {
public:
    MaterialAsset()
        : Asset(AssetType::MATERIAL)
        , m_shaderRef()
        , m_data() {}

    MaterialAsset(const MaterialData &data, AssetRef<ShaderAsset> shader)
        : Asset(AssetType::MATERIAL)
        , m_shaderRef(shader)
        , m_data(data) {}

    AssetRef<ShaderAsset> getShaderAsset();

    // Fields/textures are written to the draw state by name; which program the slots land on
    // is decided by submit(viewId, program) — the material knows nothing about programs.
    void bindFields();
    // A single material texture (or its fallback) — a depth pass with alpha-mask reads only
    // baseColorTexture; the field blob does not go there (the material layout is arbitrary).
    void bindTextureField(const char *name);
    bool hasField(const char *name) const;
    void setFieldValue(const char *name, MaterialFieldValue value);
    // A slot fed by a system every frame (world systems, flora): silent (no field — no
    // contribution), marks the field as runtime — the value lives only in memory and is not
    // written to .mat.
    void setRuntimeField(const char *name, MaterialFieldValue value);
    uint32_t getUIntFieldValue(const char *name, uint32_t defaultValue = 0) const;
    float getFloatFieldValue(const char *name, float defaultValue = 0.f) const;
    Vec4 getVec4FieldValue(const char *name, Vec4 defaultValue = Vec4()) const; // VEC4 or COLOR

#ifdef PND_EDITOR
    MaterialData &getInputs();
    void updateFields();
    void setChanged(bool value) override;
    bool isChanged() const override;
#endif

private:
#ifdef PND_EDITOR
    bool m_isChanged = false;
#endif
    void buildFieldsUbo(Std140Buffer &ubo) const;

    MaterialData m_data;
    // Cache of the assembled MATERIAL_FIELDS UBO: rebuilt only after a field change
    // (bindFields is called on every submit of every mesh — a hot path).
    mutable std::vector<uint8_t> m_cachedFieldsUbo;
    mutable bool m_fieldsUboDirty = true;
    // The MaterialSystemValues version the cache was built with: editing a system value
    // (same-named fields are set by the engine) lazily rebuilds the UBOs of all materials.
    mutable uint32_t m_systemValuesVersion = 0;
    AssetRef<ShaderAsset> m_shaderRef;
};

} // namespace Panda
