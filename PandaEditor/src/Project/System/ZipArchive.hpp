#pragma once

// zip archives of the editor: the plugin packages of the registry (context/infrastructure.md,
// section 3) and the editor update on Windows (section 5). On miniz (Vendor/miniz). Entry names
// are UTF-8 with forward slashes; an archive is never allowed to write outside its destination
// (no absolute names, no "..").

#include <Panda/Assets/Base/Path.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Panda::ZipArchive {

// Unpacks every entry into destination (created when missing). False: error says what failed; the
// files written so far stay.
bool extract(const path_t &archive, const path_t &destination, std::string &error);

// The bytes of one entry (its name as in the archive), without unpacking the rest: plugin.json of
// a package before it is installed.
std::optional<std::vector<uint8_t>> read(const path_t &archive, const std::string &entryName, std::string &error);

// Packs a folder: its own name is the root entry, everything inside follows (.DS_Store left out).
bool create(const path_t &folder, const path_t &archive, std::string &error);

// The names of the entries, in archive order.
std::optional<std::vector<std::string>> list(const path_t &archive, std::string &error);

} // namespace Panda::ZipArchive
