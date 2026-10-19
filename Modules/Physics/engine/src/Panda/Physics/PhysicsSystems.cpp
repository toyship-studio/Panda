#include "Panda/Physics/PhysicsExtensions.hpp"
#include "Panda/Physics/Physics2D.hpp"
#include "Panda/Physics/Physics3D.hpp"
#include "Panda/Physics/PhysicsComponents.hpp"

#include <Panda/Extensions/WorldSystems.hpp>
#include <Panda/GameLogic/World.hpp>

namespace Panda::Physics {

namespace {

    // The two simulations of a running world live in its context (World::context<Physics2D>,
    // <Physics3D>) between the start and the stop hooks; outside of them the context has no
    // physics and every hook below is a no-op.

    void start(World &world) {
        world.context<Physics2D>().init(&world);
        world.context<Physics3D>().init(&world);
    }

    // The destructors shut the box2d/box3d worlds down; the bodies leave the world's tracked
    // count with them (EntityBodies::clear).
    void stop(World &world) {
        world.eraseContext<Physics3D>();
        world.eraseContext<Physics2D>();
    }

    // Fixed, first: the first step of a frame restores the exact body poses after the
    // interpolated write of the previous frame; then both worlds open the step (the scripts'
    // fixedUpdate runs between the opening and the step).
    void begin(World &world, double) {
        Physics2D *physics2D = world.findContext<Physics2D>();
        Physics3D *physics3D = world.findContext<Physics3D>();
        if (world.isFirstFixedStep()) {
            if (physics2D) { physics2D->writeTransforms(&world, 1.f); }
            if (physics3D) { physics3D->writeTransforms(&world, 1.f); }
        }
        if (physics2D) { physics2D->beginStep(); }
        if (physics3D) { physics3D->beginStep(); }
    }

    // Fixed, last ("physics.step"): the step of both worlds with its contact events; the
    // transforms hold the exact poses afterwards.
    void step(World &world, double stepTime) {
        if (Physics2D *physics2D = world.findContext<Physics2D>()) { physics2D->step(&world, stepTime); }
        if (Physics3D *physics3D = world.findContext<Physics3D>()) { physics3D->step(&world, stepTime); }
    }

    // PostPhysics: the frame falls between two steps, the bodies are drawn in between.
    void interpolate(World &world, double) {
        const float alpha = world.fixedStepAlpha();
        if (Physics2D *physics2D = world.findContext<Physics2D>()) { physics2D->writeTransforms(&world, alpha); }
        if (Physics3D *physics3D = world.findContext<Physics3D>()) { physics3D->writeTransforms(&world, alpha); }
    }

    void entityRemoved(World &world, Entity entity) {
        if (Physics2D *physics2D = world.findContext<Physics2D>()) { physics2D->entityRemoved(entity); }
        if (Physics3D *physics3D = world.findContext<Physics3D>()) { physics3D->entityRemoved(entity); }
    }

    void entityRestored(World &world, Entity entity) {
        if (Physics2D *physics2D = world.findContext<Physics2D>()) { physics2D->entityRestored(entity); }
        if (Physics3D *physics3D = world.findContext<Physics3D>()) { physics3D->entityRestored(entity); }
    }

    template<typename... TComponents>
    bool isAnyOf(entt::id_type typeId) {
        return ((typeId == entt::type_hash<TComponents>::value()) || ...);
    }

    // A body or a collider came or went: the body of the entity is created, removed or rebuilt.
    // The joints are a part of the world's start only (Physics2D/3D::createJoints).
    void componentsChanged(World &world, Entity entity, entt::id_type typeId) {
        if (isAnyOf<Rigidbody2DComponent, BoxCollider2DComponent>(typeId)) {
            if (Physics2D *physics2D = world.findContext<Physics2D>()) { physics2D->componentsUpdated(entity); }
        }
        if (isAnyOf<Rigidbody3DComponent, BoxCollider3DComponent, SphereCollider3DComponent, CapsuleCollider3DComponent>(
                typeId
            )) {
            if (Physics3D *physics3D = world.findContext<Physics3D>()) { physics3D->componentsUpdated(entity); }
        }
    }

    // The transform was set from outside the simulation: the body is teleported where the
    // transform puts it; a scale change rebuilds the shapes, the scale is a part of them.
    void transformChanged(World &world, Entity entity, bool scale) {
        Physics2D *physics2D = world.findContext<Physics2D>();
        Physics3D *physics3D = world.findContext<Physics3D>();
        if (scale) {
            if (physics2D) { physics2D->propertiesUpdated(entity); }
            if (physics3D) { physics3D->propertiesUpdated(entity); }
        } else {
            if (physics2D) { physics2D->syncTransform(entity); }
            if (physics3D) { physics3D->syncTransform(entity); }
        }
    }

} // namespace

void registerSystems(WorldSystemRegistry &registry) {
    // "physics": the life cycle of the simulations and the opening of a step; the systems that
    // put their bodies into the physics world (the terrain) name it in `after`.
    WorldSystem physics;
    physics.name = "physics";
    physics.fixed = begin;
    physics.postPhysics = interpolate;
    physics.start = start;
    physics.stop = stop;
    physics.entityRemoved = entityRemoved;
    physics.entityRestored = entityRestored;
    physics.componentsChanged = componentsChanged;
    physics.transformChanged = transformChanged;
    registry.add(std::move(physics));

    // "physics.step": the step itself, after the scripts' fixedUpdate that applies the forces
    // (the core "scripts" system puts itself before it).
    WorldSystem stepSystem;
    stepSystem.name = "physics.step";
    stepSystem.fixed = step;
    stepSystem.after = {"physics"};
    registry.add(std::move(stepSystem));
}

} // namespace Panda::Physics
