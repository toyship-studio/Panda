#include "Panda/Renderer/SceneWind.hpp"
#include "Panda/Application/Application.hpp"
#include "Panda/Assets/MaterialSystemValues.hpp"

#include <algorithm>
#include <cmath>

namespace Panda {

namespace {

    SceneWindSettings s_settings;
    double s_lastTime = -1.0;   // Application::getTime anchor (the time is constant within a frame)
    double s_gustTravel = 0.0;  // travel of the gust wave (m), wrapped at 3003
    double s_flutterTime = 0.0; // flutter phase, wrapped at 3003

    // The phases accumulate lazily by the application time (not a hook in Application): the first
    // call of a frame advances them by dt, the rest see the same time — dt = 0. The clamp of 0.05 —
    // a hitch/pause does not teleport the picture.
    void advancePhases() {
        Application *application = Application::get();
        if (!application) { return; }
        const double now = static_cast<double>(application->getTime());
        if (s_lastTime < 0.0) { s_lastTime = now; }
        if (now <= s_lastTime) { return; }
        const double dt = std::min(now - s_lastTime, 0.05);
        s_lastTime = now;
        // The accumulation rates follow the grass model: Speed increases both the travel of the
        // waves and the flutter tempo; a nonzero base — in a "calm" the field does not freeze dead.
        s_gustTravel = std::fmod(s_gustTravel + dt * (1.2 + s_settings.speed * 1.2), 3003.0);
        s_flutterTime = std::fmod(s_flutterTime + dt * (1.3 + s_settings.speed * 0.9), 3003.0);
    }

} // namespace

void SceneWind::apply(const SceneWindSettings &settings) {
    s_settings = settings;
    // set dedups by value — apply may be called every frame; the hint is shown in the
    // editor at a disabled material field (next to it — a link button to the settings).
    MaterialSystemValues::set("windStrength", settings.strength, "Set by scene wind", "WIND");
    MaterialSystemValues::set("windGustiness", settings.gustiness, "Set by scene wind", "WIND");
}

const SceneWindSettings &SceneWind::settings() {
    return s_settings;
}

Vec4 SceneWind::phaseVector() {
    advancePhases();
    const float rad = s_settings.directionDegrees * 0.01745329252f;
    return Vec4(
        std::cos(rad), std::sin(rad), static_cast<float>(s_gustTravel),
        static_cast<float>(s_flutterTime)
    );
}

} // namespace Panda
