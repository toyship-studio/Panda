#pragma once
// Writing a file so that it is never left cut: what a person makes in a program — a world, a
// material, the settings — is written whole or not at all.

#include <filesystem>
#include <string>
#include <string_view>

namespace Fern {

// Writes `bytes` as the file at `path`, whole, or leaves the file as it was. The bytes go into a
// new file in the same folder (".<name>.<process>-<n>~": scrap to whoever watches the folder),
// are flushed to the drive, and that file then takes the place of the old one in one step (a
// rename): a crash, a full disk or an error of the drive on the way never leaves half a file.
// A link is followed — the file it points to is written, the link stays a link; the permissions
// of the old file are kept; a read-only file is refused, as a write in place would be.
// false — nothing changed; `error`, when given, says why in words for the log.
bool writeFileAtomically(const std::filesystem::path &path, std::string_view bytes, std::string *error = nullptr);

} // namespace Fern
