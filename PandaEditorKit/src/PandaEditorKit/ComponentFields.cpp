#include "PandaEditorKit/ComponentFields.hpp"

#include <Panda/Serialization/WorldMapper.hpp>
#include <Rain/Value.hpp>

#include <cctype>

namespace Editor {

namespace {

    std::vector<std::string> keysOf(const std::string &path) {
        std::vector<std::string> keys;
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const size_t end = slash == std::string::npos ? path.size() : slash;
            if (end > start) { keys.push_back(path.substr(start, end - start)); }
            if (slash == std::string::npos) { break; }
            start = slash + 1;
        }
        return keys;
    }

    bool isIndex(const std::string &key) {
        if (key.empty()) { return false; }
        for (const char c : key) {
            if (!std::isdigit(static_cast<unsigned char>(c))) { return false; }
        }
        return true;
    }

    // The value at a path; nullptr — there is nothing there.
    const Rain::Value *at(const Rain::Value *value, const std::vector<std::string> &keys) {
        for (const std::string &key : keys) {
            if (value == nullptr) { return nullptr; }
            if (value->isObject()) {
                value = value->find(key);
            } else if (value->isArray() && isIndex(key)) {
                const size_t index = std::stoul(key);
                value = index < value->size() ? &value->items()[index] : nullptr;
            } else if (value->isArray()) {
                // A list of things with names: the one of this name.
                const Rain::Value *found = nullptr;
                for (const Rain::Value &item : value->items()) {
                    const Rain::Value *name = item.find("name");
                    if (name != nullptr && name->isString() && name->getString() == key) { found = &item; }
                }
                value = found;
            } else {
                return nullptr;
            }
        }
        return value;
    }

} // namespace

struct ComponentFields::Values {
    std::vector<Rain::Value> documents;
    std::vector<const Rain::Value *> components; // the value of the component in each document
};

ComponentFields::ComponentFields(const std::vector<Panda::Entity> &entities, const Panda::ComponentType &type)
    : m_values(std::make_unique<Values>()) {
    m_values->documents.reserve(entities.size());
    for (Panda::Entity entity : entities) {
        if (!entity.isValid() || !entity.hasComponent(type.typeId)) { continue; }
        m_values->documents.push_back(Panda::WorldMapper::saveComponent(entity, type));
    }
    for (const Rain::Value &document : m_values->documents) { m_values->components.push_back(document.find(type.name)); }
}

ComponentFields::~ComponentFields() = default;

bool ComponentFields::differ(const std::string &path) const {
    const std::vector<std::string> keys = keysOf(path);
    const std::vector<const Rain::Value *> &components = m_values->components;
    if (components.size() < 2) { return false; }
    const Rain::Value *first = at(components.front(), keys);
    for (size_t i = 1; i < components.size(); i++) {
        const Rain::Value *other = at(components[i], keys);
        if ((first == nullptr) != (other == nullptr) || (first != nullptr && *first != *other)) { return true; }
    }
    return false;
}

glm::bvec3 ComponentFields::differAxes(const std::string &path) const {
    const Rain::Value *first = m_values->components.empty() ? nullptr : at(m_values->components.front(), keysOf(path));
    const bool list = first != nullptr && first->isArray();
    const char *axes[3][2] = {{"x", "0"}, {"y", "1"}, {"z", "2"}};
    glm::bvec3 result(false);
    for (int axis = 0; axis < 3; axis++) { result[axis] = differ(path + "/" + axes[axis][list ? 1 : 0]); }
    return result;
}

bool ComponentFields::has(const std::string &path) const {
    const std::vector<std::string> keys = keysOf(path);
    for (const Rain::Value *component : m_values->components) {
        if (at(component, keys) != nullptr) { return true; }
    }
    return m_values->components.empty(); // nobody has the component: nothing to say against the row
}

} // namespace Editor
