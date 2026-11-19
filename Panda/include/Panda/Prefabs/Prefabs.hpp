#pragma once

// Prefabs on the authoring side (context/prefabs.md): the two forms of an instance — an entry
// of a file, entities of a world — and everything that follows from "an instance is its prefab
// plus what differs". Compiled with the editor API only: a game is cooked from expanded worlds
// and has no instances to read.
#ifdef PND_EDITOR

#    include "Panda/GameLogic/World.hpp"
#    include "Panda/Prefabs/PrefabAsset.hpp"

#    include <rapidjson/document.h>

#    include <functional>
#    include <string>
#    include <vector>

namespace Panda::Prefabs {

using Json = rapidjson::Value;
using JsonAllocator = rapidjson::Document::AllocatorType;

// ---- The files ------------------------------------------------------------------------------

// A world from the text of its file (a `.world`, or a `.prefab` — a fragment with one tree): the
// instances the file holds become entities. false — the text is not JSON, the world is untouched.
bool load(World &world, const std::string &text);
// The text of the file of a world: its instances become entries again, each with what it
// differs in from its prefab. With `root` — the text of a prefab file: that tree alone, as the
// root of the file.
std::string save(World &world, Entity root = Entity());
// The template of a prefab from the text of its file (the asset handler of the editor, when it
// loads the asset). A prefab that holds itself, directly or through another, is refused: the
// instance of itself stays a lone entity.
void loadTemplate(AssetId prefab, const std::string &text, PrefabAsset &asset);

// The same as text → text, for a file that is read without the editor around it (a tool, a
// check). expand: false — not JSON. The expanded text names the entities of an instance, in the
// references between them, by the ids of the prefab: `load` is what brings them to the world.
bool expand(const std::string &file, std::string &expanded);
std::string collapse(const std::string &expanded);

// ---- The patch of an object -----------------------------------------------------------------
// What an instance differs in from its prefab is a patch of the object a world file would hold
// for the entity. An object is compared key by key (a key of the patch replaces or adds, null
// removes); an object of `x`, `y`, `z`, `w` is one value; an array of objects with different
// `name`s is compared by name and written in the patch as an object by name; any other array
// is one value.

// false — the two are the same and `patch` is untouched.
bool diff(const Json &base, const Json &value, Json &patch, JsonAllocator &allocator);
void merge(Json &target, const Json &patch, JsonAllocator &allocator);

// ---- An instance in a world -----------------------------------------------------------------

// The root entity of the instance the entity is a part of; invalid — it is a part of none.
Entity instanceRoot(Entity entity);
// The prefab of the instance the entity is a part of; 0 — none.
AssetId prefabOf(Entity entity);

// The entry a world file holds for the instance — the prefab and what the instance differs in —
// as JSON text. Empty — `root` is not the root of an instance whose prefab is there.
std::string entryOf(Entity root);
// The instance becomes what an entry says, in place: its entities keep their ids, one that the
// entry does not have any more stays in the world as a tombstone (the undo history may hold
// it), one that is back is the same entity again. The dirty flag of the world is not the
// business of this call.
void setEntry(Entity root, const std::string &entry);

// A part of what an instance differs in: all of it (an invalid entity), one entity of it, one
// component of that entity (the name of its type), or one place of that component — `field`, a
// path of keys with `/` between them: `intensity`, `camera/fieldOfView`,
// `scripts/Enemy/scriptFields/speed`. The component `parent` names a move: the entity stands
// under another one than its prefab says (taking it back takes the transform of the move too).
struct Scope final {
    Entity entity;
    std::string component;
    std::string field;
};

// What an entity of an instance differs in from its prefab, as the editor shows it.
struct ComponentDifference final {
    enum class Kind { Changed, Added, Removed };
    std::string component;           // the name of the type
    Kind kind = Kind::Changed;
    std::vector<std::string> fields; // of a changed one: the places that differ (Scope::field)
};
struct RemovedEntity final {
    UUID source = 0; // its id in the prefab
    std::string name;
};
struct Difference final {
    bool isPart = false; // the entity is a part of an instance whose prefab is there
    std::vector<ComponentDifference> components;
    bool moved = false;                // it stands under another entity than its prefab says
    std::vector<RemovedEntity> removed; // asked of a root: the entities of the prefab the instance has no more

    bool any() const {
        return !components.empty() || moved || !removed.empty();
    }
    const ComponentDifference *find(const std::string &component) const;
    // The place of the component differs: itself, something in it, or the component as a whole.
    bool differs(const std::string &component, const std::string &field) const;
};
Difference differenceOf(Entity entity);
// How many entities of the instance differ from the prefab, the removed ones counted.
size_t differenceCount(Entity root);

// The entry of the instance without the part of the difference the scope names: what Revert sets.
std::string entryWithout(Entity root, const Scope &scope);
// The entry with a removed entity of the prefab back.
std::string entryRestoring(Entity root, UUID source);

// What an Apply wrote: the new text of the prefab file and what it took from the instance.
struct Applied final {
    AssetId prefab = 0;
    UUID instance = 0;   // the root of the instance it was taken from
    std::string file;    // the text of the prefab file with the difference in it
    std::string patches; // JSON: [{"entity": <path in the prefab>, …the patch that was applied}]

    bool any() const {
        return !file.empty();
    }
};
// The part of the difference the scope names, written into the prefab: `file` is the text the
// prefab file has now. With the whole instance the entities it removed leave the prefab, the
// components it added or took off come and go there too, and the plain entities added under it
// become entities of the prefab (and parts of the instance). Nothing differs — `any()` is false.
Applied apply(Entity root, const Scope &scope, const std::string &file);
// Apply Forced: the keys `applied` touched, taken out of the other instances of the prefab in
// the text of a file — they show the applied value from now on. false — the file holds none of
// them and `result` is untouched.
bool stripApplied(const Applied &applied, const std::string &file, std::string &result);

// The entities of a world become a prefab: the text of a `.prefab` file with `root` and
// everything under it, their ids kept.
std::string createFrom(Entity root);
// …and stand in the world as an instance of it: the root keeps its id, the entities under it
// get the ids of an instance, and what named them in the world names them still.
void convertToInstance(Entity root, AssetId prefab);
// The instance becomes plain entities: nothing ties them to the prefab any more. An instance of
// another prefab nested in it is an instance of its own from now on.
void unpack(Entity root);

// ---- What is open ---------------------------------------------------------------------------

// A prefab changed — it was saved, its file changed on disk, an Apply wrote it — or is gone.
// `reload` is asked to read a prefab again (the changed one, and every prefab that holds it);
// the instances of those in the given worlds are then brought to the new prefabs, each keeping
// what it differed in. `forced` — the Apply Forced that made the change: the other instances of
// its prefab lose the keys it applied first. The dirty flags of the worlds stay as they were —
// their files hold the same entries — except where an Apply took something from an instance.
void prefabChanged(
    AssetId prefab, const std::vector<World *> &worlds, const std::function<void(AssetId)> &reload,
    const Applied *forced = nullptr
);
// The loaded scripts changed: the templates follow their classes like the worlds do.
void scriptsReloaded();
// The project closes: the templates go.
void clear();
// `prefab` holds `other` in it, directly or through a prefab it holds — or is it. An instance
// of `prefab` inside `other` would hold itself.
bool dependsOn(AssetId prefab, AssetId other);
// The prefabs the world has instances of.
std::vector<AssetId> prefabsIn(World &world);

} // namespace Panda::Prefabs

#endif // PND_EDITOR
