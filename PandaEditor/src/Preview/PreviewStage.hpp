#pragma once

#include "Preview/AssetPreview.hpp"

#include <Panda/Assets/Base/AssetRef.hpp>
#include <Panda/Assets/RuntimeMeshAsset.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/WorldView.hpp>

#include <Miren/Miren.hpp>

namespace Panda {

// The stage of a preview: a little world of its own with one asset in it and a studio light. A
// material stands there on a primitive, a model as the scene of its file. It is a world like
// any other — a WorldView draws it — so a preview shows what a scene would: the same materials
// and node transforms, the same passes of the modules, and whatever a world learns to show
// later. The camera orbits the middle of what stands on the stage (PreviewOrbit).
//
// A material on a shader of a module that needs vertices of its own (EditorExtensions::
// materialPreviews) gets the primitive in that format, as a runtime mesh of the stage.
class PreviewStage final {
public:
    PreviewStage();
    PreviewStage(const PreviewStage &) = delete;
    PreviewStage &operator=(const PreviewStage &) = delete;

    // Puts the asset on the stage in place of what was there: a material on a primitive of the
    // shape, a model as its scene. false — the asset is neither, or is not ready (a shader that
    // still compiles): the stage is left empty.
    bool show(AssetId asset, PrimitiveMeshKind shape);
    // Nothing on the stage: the assets it showed are no longer held.
    void clear();
    bool isEmpty() const {
        return m_asset == AssetId(0);
    }
    AssetId asset() const {
        return m_asset;
    }
    PrimitiveMeshKind shape() const {
        return m_shape;
    }

    // One frame of the stage through the view (its size is the caller's), seen through the
    // orbit, for a picture of this aspect.
    void draw(WorldView &view, const PreviewOrbit &orbit, float aspect);
    // The world of the stage: what the look of its frame is taken from.
    World &world() {
        return m_world;
    }

    // How a kind of asset is looked at first: a material from the front, a model from a
    // three-quarter view.
    static PreviewOrbit defaultOrbit(AssetType type);

private:
    void setUpStudio();
    bool showMaterial(AssetId asset, PrimitiveMeshKind shape);
    bool showModel(AssetId asset);
    void measure(bool round);

    World m_world;
    AssetId m_asset = 0;
    PrimitiveMeshKind m_shape = PrimitiveMeshKind::Sphere;
    // What the camera looks at and fits into the frame: the middle of the subject, how far it
    // reaches from its vertical axis whichever way it is turned, and its half height.
    glm::vec3 m_center = glm::vec3(0.f);
    float m_reach = 0.5f;
    float m_halfHeight = 0.5f;
    // A material of a module rule: the primitive in the vertices of the rule, and the rule
    // whose placeholder inputs the view gets.
    AssetRef<RuntimeMeshAsset> m_ruleMesh;
    int m_rule = -1;
};

} // namespace Panda
