// Prefabs (Prefabs.hpp): the JSON a patch is made of, and the patch of an object.

#include "Panda/Prefabs/Prefabs.hpp"

#ifdef PND_EDITOR

#    include "Panda/Prefabs/PrefabsInternal.hpp"

namespace Panda::Prefabs::Detail {

/* ------------------------------ JSON ------------------------------ */

UUID idOf(const Json &value) {
    return value.isUint() ? UUID(value.getUint()) : UUID(0);
}

UUID idIn(const Json &object, const char *key) {
    const Json *value = object.find(key);
    return value != nullptr ? idOf(*value) : UUID(0);
}

UUID parentOf(const Json &entity) {
    const Json *relationship = entity.find(RELATIONSHIP);
    return relationship != nullptr ? idIn(*relationship, PARENT) : UUID(0);
}

bool isVector(const Json &value) {
    if (!value.isObject() || value.size() == 0) { return false; }
    for (auto member = value.members().begin(); member != value.members().end(); ++member) {
        const char *name = member->key.c_str();
        if (member->key.size() != 1 || (name[0] != 'x' && name[0] != 'y' && name[0] != 'z' && name[0] != 'w')) {
            return false;
        }
    }
    return true;
}

bool isNamed(const Json &value) {
    if (!value.isArray()) { return false; }
    std::set<std::string> names;
    for (const Json &item : value.items()) {
        const Json *name = item.find(NAME);
        if (name == nullptr || !name->isString() || !names.insert(name->getString()).second) { return false; }
    }
    return true;
}

bool isReserved(const char *key) {
    return std::strcmp(key, ID) == 0 || std::strcmp(key, RELATIONSHIP) == 0 || std::strcmp(key, PREFAB_LINK_KEY) == 0 ||
           std::strcmp(key, MISSING) == 0;
}

void diffEntity(const Json &base, const Json &value, Json &patch) {
    for (auto member = value.members().begin(); member != value.members().end(); ++member) {
        const char *key = member->key.c_str();
        if (isReserved(key)) { continue; }
        const Json *baseValue = base.find(key);
        Json part;
        if (baseValue == nullptr) {
            part = Json(member->value);
        } else if (!diff(*baseValue, member->value, part)) {
            continue;
        }
        patch.add(key, std::move(part));
    }
    for (auto member = base.members().begin(); member != base.members().end(); ++member) {
        const char *key = member->key.c_str();
        if (!isReserved(key) && value.find(key) == nullptr) { patch.add(key, Json()); }
    }
}

Json *descend(Json &patch, const std::vector<std::string> &keys, size_t count) {
    Json *at = &patch;
    for (size_t i = 0; i < count && at != nullptr; i++) { at = at->find(keys[i].c_str()); }
    return at;
}

std::vector<std::string> split(const std::string &path) {
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

void prune(Json &entry) {
    Json *list = entry.find(OVERRIDES);
    if (list == nullptr || !list->isArray()) { return; }
    for (auto item = list->items().begin(); item != list->items().end();) {
        item = item->size() <= 1 ? list->items().erase(item) : item + 1;
    }
    if (list->empty()) { entry.erase(OVERRIDES); }
}

bool isOwnOfRoot(const char *key) {
    return isReserved(key) || std::strcmp(key, PREFAB) == 0 || std::strcmp(key, OVERRIDES) == 0 || std::strcmp(key, TAG) == 0;
}

} // namespace Panda::Prefabs::Detail

namespace Panda::Prefabs {

using namespace Detail;

/* ------------------------------ The patch of an object ------------------------------ */

bool diff(const Json &base, const Json &value, Json &patch) {
    if (base.isObject() && value.isObject() && !isVector(base) && !isVector(value)) {
        Json result = Json::object();
        for (auto member = value.members().begin(); member != value.members().end(); ++member) {
            const Json *baseValue = base.find(member->key.c_str());
            Json part;
            if (baseValue == nullptr) {
                part = Json(member->value);
            } else if (!diff(*baseValue, member->value, part)) {
                continue;
            }
            result.add(member->key.c_str(), std::move(part));
        }
        for (auto member = base.members().begin(); member != base.members().end(); ++member) {
            if (value.find(member->key.c_str()) == nullptr) { result.add(member->key.c_str(), Json()); }
        }
        if (result.size() == 0) { return false; }
        patch = std::move(result);
        return true;
    }
    if (base.isArray() && value.isArray() && isNamed(base) && isNamed(value) && !(base.empty() && value.empty())) {
        Json result = Json::object();
        for (const Json &item : value.items()) {
            const std::string &name = item.find(NAME)->getString();
            const Json *baseItem = named(base, name);
            Json part;
            if (baseItem == nullptr) {
                part = Json(item);
                part.erase(NAME); // the key of the patch says it
            } else if (!diff(*baseItem, item, part)) {
                continue;
            }
            result.add(name, std::move(part));
        }
        for (const Json &item : base.items()) {
            const std::string &name = item.find(NAME)->getString();
            if (named(value, name) == nullptr) { result.add(name, Json()); }
        }
        if (result.size() == 0) { return false; }
        patch = std::move(result);
        return true;
    }
    if (base == value) { return false; }
    patch = Json(value);
    return true;
}

void merge(Json &target, const Json &patch) {
    if (patch.isObject() && target.isObject() && !isVector(target) && !isVector(patch)) {
        for (auto member = patch.members().begin(); member != patch.members().end(); ++member) {
            const char *key = member->key.c_str();
            if (member->value.isNull()) {
                target.erase(key);
            } else if (Json *own = target.find(key)) {
                merge(*own, member->value);
            } else {
                target.add(key, Json(member->value));
            }
        }
        return;
    }
    if (patch.isObject() && target.isArray() && isNamed(target) && !isVector(patch)) {
        for (auto member = patch.members().begin(); member != patch.members().end(); ++member) {
            const char *name = member->key.c_str();
            Json *own = named(target, name);
            if (member->value.isNull()) {
                if (own != nullptr) { target.items().erase(target.items().begin() + (own - target.items().data())); }
            } else if (own != nullptr) {
                merge(*own, member->value);
            } else if (member->value.isObject()) {
                Json item = Json::object();
                item.add(NAME, Json(name));
                for (auto field = member->value.members().begin(); field != member->value.members().end(); ++field) {
                    if (std::strcmp(field->key.c_str(), NAME) != 0) {
                        item.add(field->key.c_str(), Json(field->value));
                    }
                }
                target.push(std::move(item));
            }
        }
        return;
    }
    target = Json(patch);
}

} // namespace Panda::Prefabs

#endif // PND_EDITOR
