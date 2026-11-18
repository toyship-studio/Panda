#pragma once

#include <Panda/Assets/Base/Path.hpp>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace Panda {

// A place in a file that a line of a build's output names — the compiler's
// "<file>:<line>:<column>: error: …" and "In file included from <file>:<line>:", cmake's
// "CMake Error at <file>:<line> (message):", the "<file>(<line>,<column>): error …" of Visual
// Studio.
//
// A place is one only when its file is there. That is what tells it from a time of day, a
// version or a piece of code that is written the same way — whatever tool wrote the line, in
// whatever words: no list of the messages of the compilers is kept here.
struct BuildOutputLocation {
    path_t file;       // absolute
    int line = 0;      // from 1
    int column = 0;    // from 1; 0 — the line names none
    size_t begin = 0;  // where the place is written in the text of the line: [begin, end)
    size_t end = 0;
};

// The first place the line names. A path that is not absolute is looked for in `folders` — where
// the build runs and where its cmake project is.
std::optional<BuildOutputLocation> findBuildOutputLocation(std::string_view text, const std::vector<path_t> &folders);

} // namespace Panda
