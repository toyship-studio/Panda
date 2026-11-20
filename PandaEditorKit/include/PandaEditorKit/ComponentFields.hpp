#pragma once

#include <Panda/Extensions/ComponentTypes.hpp>
#include <Panda/GameLogic/Entity.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// The fields of one component of several entities as their file has them — the component as a
// JSON value (WorldMapper::saveComponent) — for the form of the entities to compare field by
// field. A field is named by its path in that value, the way a row of the form names it
// (PropertyRow::field):
// `intensity`, `camera/fieldOfView`, `gravity/1`; a list of things with names is gone through by
// the names (`scripts/Enemy/scriptFields/speed`). The entities that do not have the component
// take no part.
class ComponentFields final {
public:
    ComponentFields(const std::vector<Panda::Entity> &entities, const Panda::ComponentType &type);
    ~ComponentFields();
    ComponentFields(const ComponentFields &) = delete;
    ComponentFields &operator=(const ComponentFields &) = delete;

    // The entities differ in the field: in its value, or in having it at all.
    bool differ(const std::string &path) const;
    // …axis by axis, for a vector: x, y and z of an object, the first three items of a list.
    glm::bvec3 differAxes(const std::string &path) const;
    // Some entity has the field. A row that names a field nobody has names it wrong.
    bool has(const std::string &path) const;

private:
    struct Values;
    std::unique_ptr<Values> m_values;
};

} // namespace Editor
