#include "PandaEditorKit/ComponentFields.hpp"

#include "PandaEditorKit/ComponentCommands.hpp"

#include <rapidjson/document.h>

#include <cctype>

namespace Editor {

namespace {

    using Json = rapidjson::Value;

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
    const Json *at(const Json *value, const std::vector<std::string> &keys) {
        for (const std::string &key : keys) {
            if (value == nullptr) { return nullptr; }
            if (value->IsObject()) {
                const auto member = value->FindMember(key.c_str());
                value = member != value->MemberEnd() ? &member->value : nullptr;
            } else if (value->IsArray() && isIndex(key)) {
                const size_t index = std::stoul(key);
                value = index < value->Size() ? &(*value)[static_cast<rapidjson::SizeType>(index)] : nullptr;
            } else if (value->IsArray()) {
                // A list of things with names: the one of this name.
                const Json *found = nullptr;
                for (const Json &item : value->GetArray()) {
                    if (!item.IsObject()) { continue; }
                    const auto name = item.FindMember("name");
                    if (name != item.MemberEnd() && name->value.IsString() && key == name->value.GetString()) { found = &item; }
                }
                value = found;
            } else {
                return nullptr;
            }
        }
        return value;
    }

} // namespace

struct ComponentFields::Texts {
    std::vector<rapidjson::Document> documents;
    std::vector<const Json *> components; // the object of the component in each document
};

ComponentFields::ComponentFields(const std::vector<Panda::Entity> &entities, const Panda::ComponentType &type)
    : m_texts(std::make_unique<Texts>()) {
    m_texts->documents.reserve(entities.size());
    for (Panda::Entity entity : entities) {
        if (!entity.isValid() || !entity.hasComponent(type.typeId)) { continue; }
        rapidjson::Document &document = m_texts->documents.emplace_back();
        const std::string text = componentText(entity, type);
        document.Parse(text.c_str(), text.size());
    }
    for (const rapidjson::Document &document : m_texts->documents) {
        const Json *component = nullptr;
        if (!document.HasParseError() && document.IsObject()) {
            const auto member = document.FindMember(type.name.c_str());
            if (member != document.MemberEnd()) { component = &member->value; }
        }
        m_texts->components.push_back(component);
    }
}

ComponentFields::~ComponentFields() = default;

bool ComponentFields::differ(const std::string &path) const {
    const std::vector<std::string> keys = keysOf(path);
    const std::vector<const Json *> &components = m_texts->components;
    if (components.size() < 2) { return false; }
    const Json *first = at(components.front(), keys);
    for (size_t i = 1; i < components.size(); i++) {
        const Json *other = at(components[i], keys);
        if ((first == nullptr) != (other == nullptr) || (first != nullptr && !(*first == *other))) { return true; }
    }
    return false;
}

glm::bvec3 ComponentFields::differAxes(const std::string &path) const {
    const Json *first = m_texts->components.empty() ? nullptr : at(m_texts->components.front(), keysOf(path));
    const bool list = first != nullptr && first->IsArray();
    const char *axes[3][2] = {{"x", "0"}, {"y", "1"}, {"z", "2"}};
    glm::bvec3 result(false);
    for (int axis = 0; axis < 3; axis++) { result[axis] = differ(path + "/" + axes[axis][list ? 1 : 0]); }
    return result;
}

bool ComponentFields::has(const std::string &path) const {
    const std::vector<std::string> keys = keysOf(path);
    for (const Json *component : m_texts->components) {
        if (at(component, keys) != nullptr) { return true; }
    }
    return m_texts->components.empty(); // nobody has the component: nothing to say against the row
}

} // namespace Editor
