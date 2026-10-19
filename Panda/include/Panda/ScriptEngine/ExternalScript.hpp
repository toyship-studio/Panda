#pragma once

#include <Foundation/Memory.hpp>
#include "Panda/Base/UUID.hpp"

#include "Panda/ScriptEngine/InnerScriptHook.hpp"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace Panda {

using ScriptFieldValue = std::variant<int32_t, float, UUID>;

inline void *getScriptFieldValuePointer(ScriptFieldValue &value) {
    return std::visit([](auto &actualValue) -> void * { return &actualValue; }, value);
}

inline const void *getScriptFieldValuePointer(const ScriptFieldValue &value) {
    return std::visit([](const auto &actualValue) -> const void * { return &actualValue; }, value);
}

struct ScriptField {
    Handle instanceId;
    Handle fieldId;
    std::string name;
    ScriptFieldType type;
    ScriptFieldValue value;

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
        ExternalCalls::setFieldValue(entityId, id, field.fieldId, getScriptFieldValuePointer(field.value));
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
