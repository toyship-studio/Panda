#pragma once

#include "Panda/Extensions/Settings.hpp"

#include <Rain/Rain.hpp>

#include <initializer_list>
#include <istream>
#include <ostream>

namespace Panda {

// The sections of one scope ↔ the current object of a coder: the top level of `.world` next to
// the entities, `.Panda/project.json`, the user's `settings.json`. Registered types come from the
// settings registry (Extensions::settings), a key without a type is kept as is (ForeignSettings).
// The format — PandaSDK/docs/world-format.md.
class SettingsMapper {
public:
    // The sections of the scope in registration order, then the foreign ones. A coder without
    // opaque values (the binary one) drops the foreign sections — logged once per call.
    static void encode(SettingsScope scope, TypeContext &store, Rain::Encoder &encoder);
    // Every registered type of the scope goes to its defaults and the foreign sections of the
    // previous content are dropped; then the keys of the object are read: a registered key — into
    // its type, an own key — left to the caller, any other — kept as foreign, with a warning.
    static void decode(
        SettingsScope scope, TypeContext &store, Rain::Decoder &decoder, std::initializer_list<const char *> ownKeys = {}
    );
    // The registered types of the scope to their defaults, the foreign sections dropped.
    static void reset(SettingsScope scope, TypeContext &store);
    // A type registered after the file was read (the parts of a module loaded later) takes its
    // section from the foreign values: decoded into the type, removed from ForeignSettings.
    static void adoptForeign(SettingsScope scope, TypeContext &store);
    // The sections of the scope from one store into another (the world copy for Play).
    static void copy(SettingsScope scope, TypeContext &from, TypeContext &to);
    // A whole file: one object of sections. load — false when the stream is not an object of the
    // coder's format (the store is untouched then).
    static bool load(SettingsScope scope, TypeContext &store, Rain::Decoder &decoder, std::istream &stream);
    static void save(SettingsScope scope, TypeContext &store, Rain::Encoder &encoder, std::ostream &stream);
};

} // namespace Panda
