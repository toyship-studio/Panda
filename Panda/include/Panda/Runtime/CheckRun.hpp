#pragma once

#include "Panda/Assets/Base/Path.hpp"

#include <cstdint>
#include <vector>

namespace Panda {

// A check run of a built game: the game plays a number of frames and leaves by itself, so a build
// is checked with nobody at the screen (a script, CI, an agent through the editor: the `export`
// tool of its MCP server, action `run`). Asked for by the environment of the process:
//   PANDA_CHECK_FRAMES=<n>    the game plays n frames, then exits — 0: it ran; 2: its data did
//                             not load; 3: it did not leave in time (PANDA_CHECK_TIMEOUT seconds,
//                             60 by default: a game that hangs does not hang the check)
//   PANDA_CHECK_SHOT=<file>   the picture of the last frame — the world and the game UI, as the
//                             window shows it — is written to the file (CheckShot)
// Without PANDA_CHECK_FRAMES the game is the game. One exit code is not of the check alone: a
// game whose data does not load leaves with 2 whenever it is started — it has nothing to show.
namespace CheckRun {
    constexpr const char *FRAMES_VARIABLE = "PANDA_CHECK_FRAMES";
    constexpr const char *SHOT_VARIABLE = "PANDA_CHECK_SHOT";
    constexpr const char *TIMEOUT_VARIABLE = "PANDA_CHECK_TIMEOUT";
    constexpr int EXIT_RAN = 0;
    constexpr int EXIT_NO_DATA = 2;
    constexpr int EXIT_TIMED_OUT = 3;
} // namespace CheckRun

// The picture of a frame as a check run writes it: RGBA8, no compression — whoever asked for it
// packs it the way it likes.
struct CheckShot final {
    uint32_t width = 0;
    uint32_t height = 0;
    bool bottomUp = false; // the first row of the pixels is the bottom of the picture
    std::vector<uint8_t> pixels;

    bool write(const path_t &file) const;
    bool read(const path_t &file);
};

} // namespace Panda
