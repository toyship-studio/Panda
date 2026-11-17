#pragma once

#include <Foundation/Memory.hpp>
#include "Panda/Base/UUID.hpp"

#include "Panda/ScriptEngine/InnerScriptHook.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Panda {

// The value of a script field: one value, or a list of them for a field that is a std::vector in
// the class. The type of the field (ScriptFieldType) says what one value is: an int32_t, a float,
// or the id of an entity or of an asset. A list has the shape of the value and no flag of its
// own: the world file writes it as an array, the inspector shows rows.
using ScriptFieldValue =
    std::variant<int32_t, float, UUID, std::vector<int32_t>, std::vector<float>, std::vector<UUID>>;

inline bool isScriptFieldList(const ScriptFieldValue &value) {
    return std::visit([](const auto &actual) { return requires { actual.size(); }; }, value);
}

// The value a field starts with: zero, no reference, an empty list.
inline ScriptFieldValue defaultScriptFieldValue(ScriptFieldType type, bool list) {
    switch (type) {
        case ScriptFieldType::FLOAT:
            return list ? ScriptFieldValue(std::vector<float>()) : ScriptFieldValue(0.f);
        case ScriptFieldType::ENTITY:
        case ScriptFieldType::TEXTURE:
        case ScriptFieldType::MATERIAL:
        case ScriptFieldType::WORLD:
        case ScriptFieldType::FONT:
        case ScriptFieldType::MESH:
            return list ? ScriptFieldValue(std::vector<UUID>()) : ScriptFieldValue(UUID(0));
        case ScriptFieldType::INTEGER:
        case ScriptFieldType::UNKNOWN:
            break;
    }
    return list ? ScriptFieldValue(std::vector<int32_t>()) : ScriptFieldValue(int32_t(0));
}

// A value as the script ABI takes it (SetFieldValueFn): an array of 4-byte items.
struct ScriptFieldItems {
    const void *data;
    uint32_t count;
};

inline ScriptFieldItems scriptFieldItems(const ScriptFieldValue &value) {
    static_assert(sizeof(UUID) == sizeof(uint32_t), "an id crosses the script ABI as 4 bytes");
    return std::visit(
        [](const auto &actual) -> ScriptFieldItems {
            if constexpr (requires { actual.size(); }) {
                return {actual.data(), static_cast<uint32_t>(actual.size())};
            } else {
                return {&actual, 1};
            }
        },
        value
    );
}

struct ScriptField {
    Handle instanceId;
    Handle fieldId;
    std::string name;
    ScriptFieldType type;
    ScriptFieldValue value;

    bool isList() const {
        return isScriptFieldList(value);
    }

    ScriptField(
        Handle instanceId, Handle fieldId, const std::string &name, ScriptFieldType type, ScriptFieldValue value
    )
        : instanceId(instanceId)
        , fieldId(fieldId)
        , name(name)
        , type(type)
        , value(value) {}

    bool operator==(const ScriptField &other) const {
        return instanceId == other.instanceId && fieldId == other.fieldId;
    }
};

class ExternalScript {
public:
    ExternalScript(Handle entityId, Handle id, const std::string &name, std::vector<ScriptField> fields, int order = 0)
        : entityId(entityId)
        , id(id)
        , name(name)
        , fields(fields)
        , order(order) {}

    void invokeStart() {
        ExternalCalls::invokeStartAtScript(entityId, id);
    }

    void invokeFixedUpdate(float stepTime) {
        ExternalCalls::invokeFixedUpdateAtScript(entityId, id, stepTime);
    }

    void invokeUpdate(float deltaTime) {
        ExternalCalls::invokeUpdateAtScript(entityId, id, deltaTime);
    }

    void invokeLateUpdate(float deltaTime) {
        ExternalCalls::invokeLateUpdateAtScript(entityId, id, deltaTime);
    }

    void invokeApplicationFocus(bool focused) {
        if (ExternalCalls::invokeApplicationFocusAtScript) {
            ExternalCalls::invokeApplicationFocusAtScript(entityId, id, focused);
        }
    }

    void invokeApplicationPause(bool paused) {
        if (ExternalCalls::invokeApplicationPauseAtScript) {
            ExternalCalls::invokeApplicationPauseAtScript(entityId, id, paused);
        }
    }

    void invokeBeginCollisionTouch(Handle other) {
        ExternalCalls::invokeBeginCollisionTouch(entityId, id, other);
    }

    void invokeEndCollisionTouch(Handle other) {
        ExternalCalls::invokeEndCollisionTouch(entityId, id, other);
    }

    void invokeBeginSensorOverlap(Handle other) {
        ExternalCalls::invokeBeginSensorOverlap(entityId, id, other);
    }

    void invokeEndSensorOverlap(Handle other) {
        ExternalCalls::invokeEndSensorOverlap(entityId, id, other);
    }

    const std::string &getName() const {
        return name;
    }

    int getOrder() const {
        return order;
    }

    void setOrder(int newOrder) {
        order = newOrder;
    }

    bool hasField(const char *name) {
        return findField(name) != fields.end();
    }

    ScriptField &getField(const char *name) {
        return *findField(name);
    }

    std::vector<ScriptField> &getFields() {
        return fields;
    }

    void addField(ScriptField field) {
        fields.emplace_back(field);
    }

    void removeField(ScriptField &field) {
        auto it = std::remove(fields.begin(), fields.end(), field);
        if (it != fields.end()) { fields.erase(it); }
    }

    bool operator==(const ExternalScript &rhs) const {
        return id == rhs.id;
    }

    // Binds the container to the script instance created for its entity. The entity is passed
    // too: a duplicated entity carries the id of its source.
    void bind(Handle _entityId, Handle _id) {
        entityId = _entityId;
        id = _id;
        for (ScriptField &field : fields) { field.instanceId = _id; }
    }

    // The instance is gone: the world does not run anymore.
    void unbind() {
        id = 0;
        for (ScriptField &field : fields) { field.instanceId = 0; }
    }

    // Writes the field value into the script instance. Without an instance nothing is written:
    // an instance gets every field when it is created.
    void applyField(ScriptField &field) {
        if (!id) { return; }
        const ScriptFieldItems items = scriptFieldItems(field.value);
        ExternalCalls::setFieldValue(entityId, id, field.fieldId, items.data, items.count);
    }

private:
    std::vector<ScriptField>::iterator findField(const char *name) {
        return std::find_if(fields.begin(), fields.end(), [&](const ScriptField &item) { return item.name == name; });
    }

    Handle id;
    Handle entityId;
    std::string name;
    std::vector<ScriptField> fields;
    int order = 0;
};

} // namespace Panda
