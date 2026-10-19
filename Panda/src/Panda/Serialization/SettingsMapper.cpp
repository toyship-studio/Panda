#include "Panda/Serialization/SettingsMapper.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <Foundation/Foundation.hpp>
#include <Rain/Coders/JsonDecoder.hpp>

#include <sstream>
#include <string>

namespace Panda {

namespace {

    const char *scopeName(SettingsScope scope) {
        switch (scope) {
            case SettingsScope::World: return "world";
            case SettingsScope::Project: return "project";
            case SettingsScope::Editor: return "editor";
        }
        return "settings";
    }

    // One object of sections: the whole file of a scope (Rain calls the static encode/decode).
    struct SettingsDocument {
        SettingsScope scope = SettingsScope::World;
        TypeContext *store = nullptr;

        static void encode(const char *key, Rain::Encoder *encoder, const SettingsDocument &document) {
            encoder->beginObject(key);
            SettingsMapper::encode(document.scope, *document.store, *encoder);
            encoder->endObject();
        }

        static bool decode(const char *key, Rain::Decoder *decoder, SettingsDocument &document) {
            if (!decoder->beginObject(key)) { return false; }
            SettingsMapper::decode(document.scope, *document.store, *decoder);
            decoder->endObject();
            return true;
        }
    };

    // One kept section as a document (adoptForeign): the type reads its key at the root.
    struct ForeignSection {
        const SettingsType *type = nullptr;
        TypeContext *store = nullptr;

        static void encode(const char *key, Rain::Encoder *encoder, const ForeignSection &section) {
            encoder->beginObject(key);
            section.type->encode(section.type->name.c_str(), *encoder, *section.store);
            encoder->endObject();
        }

        static bool decode(const char *key, Rain::Decoder *decoder, ForeignSection &section) {
            if (!decoder->beginObject(key)) { return false; }
            section.type->decode(section.type->name.c_str(), *decoder, *section.store);
            decoder->endObject();
            return true;
        }
    };

} // namespace

void SettingsMapper::encode(SettingsScope scope, TypeContext &store, Rain::Encoder &encoder) {
    for (const SettingsType &type : GameContext::getExtensions().settings.types()) {
        if (type.scope == scope) { type.encode(type.name.c_str(), encoder, store); }
    }
    ForeignSettings *foreign = store.find<ForeignSettings>();
    if (foreign == nullptr || foreign->items.empty()) { return; }
    if (!encoder.supportsRawJson()) {
        LOG_WARN(
            "%zu %s settings section(s) of modules that are not loaded are not written: the format carries only "
            "registered data",
            foreign->items.size(), scopeName(scope)
        );
        return;
    }
    for (const ForeignValue &item : foreign->items) {
        encoder.encodeRawJson(item.name.c_str(), item.json);
    }
}

void SettingsMapper::decode(
    SettingsScope scope, TypeContext &store, Rain::Decoder &decoder, std::initializer_list<const char *> ownKeys
) {
    const Extensions &extensions = GameContext::getExtensions();
    reset(scope, store);
    for (const std::string &name : decoder.getMemberKeys()) {
        bool own = false;
        for (const char *ownKey : ownKeys) {
            if (name == ownKey) { own = true; }
        }
        if (own) { continue; }
        if (const SettingsType *type = extensions.settings.find(scope, name)) {
            type->decode(name.c_str(), decoder, store);
            continue;
        }
        std::string json; // a section of a module that is not loaded — as is
        if (decoder.decodeRawJson(name.c_str(), json)) {
            LOG_WARN("%s settings \"%s\": no module provides them, kept as they are", scopeName(scope), name.c_str());
            store.get<ForeignSettings>().items.push_back({name, std::move(json)});
        } else {
            LOG_WARN("%s settings \"%s\": no module provides them, dropped", scopeName(scope), name.c_str());
        }
    }
}

void SettingsMapper::reset(SettingsScope scope, TypeContext &store) {
    for (const SettingsType &type : GameContext::getExtensions().settings.types()) {
        if (type.scope == scope) { type.reset(store); }
    }
    store.erase<ForeignSettings>();
}

void SettingsMapper::adoptForeign(SettingsScope scope, TypeContext &store) {
    ForeignSettings *foreign = store.find<ForeignSettings>();
    if (foreign == nullptr) { return; }
    const Extensions &extensions = GameContext::getExtensions();
    for (auto it = foreign->items.begin(); it != foreign->items.end();) {
        const SettingsType *type = extensions.settings.find(scope, it->name);
        if (type == nullptr) {
            ++it;
            continue;
        }
        // The section is one JSON value: read as a document of its key alone.
        Rain::JsonDecoder jsonDecoder;
        Rain::Decoder &decoder = jsonDecoder; // the stream entry is the base template
        std::istringstream stream("{\"" + it->name + "\":" + it->json + "}");
        ForeignSection section;
        section.type = type;
        section.store = &store;
        if (!decoder.decode(stream, section)) {
            LOG_WARN("%s settings \"%s\": the kept section is not readable, dropped", scopeName(scope), it->name.c_str());
        }
        it = foreign->items.erase(it);
    }
    if (foreign->items.empty()) { store.erase<ForeignSettings>(); }
}

void SettingsMapper::copy(SettingsScope scope, TypeContext &from, TypeContext &to) {
    for (const SettingsType &type : GameContext::getExtensions().settings.types()) {
        if (type.scope == scope) { type.copy(from, to); }
    }
    to.erase<ForeignSettings>();
    if (const ForeignSettings *foreign = from.find<ForeignSettings>()) { to.get<ForeignSettings>() = *foreign; }
}

bool SettingsMapper::load(SettingsScope scope, TypeContext &store, Rain::Decoder &decoder, std::istream &stream) {
    SettingsDocument document;
    document.scope = scope;
    document.store = &store;
    return decoder.decode(stream, document);
}

void SettingsMapper::save(SettingsScope scope, TypeContext &store, Rain::Encoder &encoder, std::ostream &stream) {
    SettingsDocument document;
    document.scope = scope;
    document.store = &store;
    encoder.encode(stream, document);
}

} // namespace Panda
