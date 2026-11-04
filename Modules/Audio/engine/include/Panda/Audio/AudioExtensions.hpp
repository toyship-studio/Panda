#pragma once

namespace Panda {

class Extensions;

namespace Audio {

    // Module passport from module.json (via the build).
    extern const char *const MODULE_ID;
    extern const char *const MODULE_VERSION;

    // Contributions of the audio to the engine registries: the "audio" asset kind (clips), the
    // "audio" application system (the mixer, AudioEngine), the audio source and listener
    // component types with the "audio" world system, and the "audio" script table.
    void registerExtensions(Extensions &extensions);

} // namespace Audio

} // namespace Panda
