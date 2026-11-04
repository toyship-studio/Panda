#pragma once

#include <Rain/Rain.hpp>

namespace Panda {

// "audio" of the editor scope (the user's settings.json): the sound of the editor process.
struct AudioEditorSettings final : public Rain::Codable {
    bool mute = false; // silences everything the editor plays: clip previews and Play alike

    RAIN_FIELDS_BEGIN(AudioEditorSettings)
    RAIN_FIELD(mute)
    RAIN_FIELDS_END
};

} // namespace Panda
