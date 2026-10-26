#pragma once
// The fonts installed on this machine, by family. The font folders of the system are scanned once,
// on the first call (a few hundred files; only the table directories, name and OS/2 tables are
// read — tens of milliseconds), and the result is kept for the process. Every function may be called
// from any thread; the first caller does the scan.
//
// What a face reports: the typographic family ("Inter", "JetBrains Mono"), the style ("Bold Italic"),
// the OS/2 weight class (100..900) and the italic flag, the file and the index inside a collection
// (.ttc). Variable fonts and collections are reported face by face as the files declare them; the
// rasterizer draws the default instance of a variable font.
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Fern::Fonts {

struct Face {
    std::string family;
    std::string style;
    int weight = 400; // the OS/2 usWeightClass: 400 regular, 500 medium, 700 bold
    bool italic = false;
    std::filesystem::path file;
    int index = 0; // the face inside a .ttc; 0 otherwise
};

// Every family, sorted, each once.
const std::vector<std::string> &families();
// Every face found.
const std::vector<Face> &faces();
// The face of the family closest to the weight, upright unless italic is asked for; nullopt — no such
// family. A family with one face answers with it for every weight.
std::optional<Face> find(std::string_view family, int weight = 400, bool italic = false);
// Reads the folders again (a font was installed while the application runs).
void rescan();
// The faces of one font file (a project asset): the same tables, no catalog involved.
std::vector<Face> facesOf(const std::filesystem::path &file);
// The folders scanned: the system's, then the user's.
std::vector<std::filesystem::path> directories();

} // namespace Fern::Fonts
