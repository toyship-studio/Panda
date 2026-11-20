#pragma once

#include <cstdint>

namespace Bamboo {

// How firm a haptic tap is.
enum class Haptic : uint8_t { Light, Medium, Heavy };

namespace Haptics {
    // A short tap felt in the hand: the answer of a phone to what a finger did — a block broken, a
    // thing chosen. Light is the quiet answer to an action repeated often. iOS plays it on its motor
    // and keeps to the switch of the system haptics; the editor and the desktops have no motor in
    // the hand, and the call does nothing there — a game calls it without asking where it runs.
    void play(Haptic haptic);
} // namespace Haptics

} // namespace Bamboo
