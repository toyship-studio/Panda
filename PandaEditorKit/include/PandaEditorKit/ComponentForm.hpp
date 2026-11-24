#pragma once

#include "PandaEditorKit/ComponentCommands.hpp"
#include "PandaEditorKit/EditorExtensions.hpp"
#include "PandaEditorKit/EditorHost.hpp"
#include "PandaEditorKit/Widgets/FormWidgets.hpp"

#include <PandaUI/Runtime/Context.hpp>

#include <functional>
#include <memory>
#include <string>
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

// The Main row of a component of a kind a world uses one of (BaseComponents.hpp: the camera, the
// sun, the sky, the wind): a radio button over the world — on, this component is the one the
// world uses and the others are spares; off, it is a spare. Under it, what the world does with it:
// a spare names the one the world uses; a second main one is a warning, the world uses the first.
// `kind` — what the component is to a person ("sky").
template<typename T>
void addMainRow(PandaUI::CollapsibleSection &section, const ComponentForm<T> &form, const char *kind, const char *help) {
    const auto data = form.data;
    Panda::World *world = &form.world;
    Panda::Entity entity = form.entities.front();
    PandaUI::CollapsibleSection *owner = &section; // alive while its row calls back
    addToggleRow(section, "Main", data->isMain, [data, world, entity, owner](bool on) {
        data->isMain = on;
        commitMain<T>(*world, entity, on);
        // The note under the row follows on the next tick: a form is not built again from inside
        // a control of its own.
        if (PandaUI::Context *context = owner->getContext()) {
            context->postDeferred([] { editorHost().refreshFromWorld(); });
        }
    }, "isMain")->setHelp(help);
    const Panda::Entity used = world->findMain<T>();
    if (data->isMain && used.isValid() && !(used == entity)) {
        section.addContent(makeFormWarning(
            "Another " + std::string(kind) + " is main too: the world uses " + used.getName() + ". Turn one of them off."
        ));
    } else if (!data->isMain) {
        section.addContent(makeFormHint(
            used.isValid() ? "A spare: the world uses " + used.getName() + "."
                           : "No " + std::string(kind) + " of the world is main: the world has none."
        ));
    }
}

} // namespace Editor
