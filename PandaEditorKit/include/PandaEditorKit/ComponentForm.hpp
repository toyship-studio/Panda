#pragma once

#include "PandaEditorKit/ComponentCommands.hpp"
#include "PandaEditorKit/EditorExtensions.hpp"
#include "PandaEditorKit/Widgets/FormWidgets.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace Editor {

// What a component section edits (ComponentInspector::build), for the core and for the editor
// parts of packages alike: `data` — a copy of component T of the primary target, which the rows
// of the section change — and `commit`, which writes that copy to every target that has T as
// one undoable edit (commitComponent<T>; consecutive edits of the same targets are one undo
// step). Each row names the field it edits as the file names it: the form of the entity finds
// from that what the targets differ in and what an instance of a prefab has of its own.
//
//     ComponentForm<Light> form(context);
//     auto data = form.data;
//     auto commit = form.commit;
//     addNumberRow(section, "Intensity", 0.f, 100.f, data->intensity,
//                  [data, commit](float v) { data->intensity = v; commit(); }, "intensity");
//
// The rows keep `data` and `commit` alive; the form itself may go when build returns.
template<typename T>
struct ComponentForm {
    explicit ComponentForm(const ComponentInspectorContext &context)
        : world(context.world)
        , entities(context.entities)
        , data(std::make_shared<T>(Panda::Entity(context.entities.front()).getComponent<T>()))
        , commit([world = &context.world, entities = context.entities, data = data] {
            commitComponent<T>(*world, entities, *data);
        }) {}

    Panda::World &world;
    std::vector<Panda::Entity> entities;
    std::shared_ptr<T> data;
    std::function<void()> commit;
};

} // namespace Editor
