#pragma once

#include "Project/Build/BuildOutputLocation.hpp"

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Editor {

// The output of a build as a list of lines: what cmake and the cook of a game said, each line
// colored by what it is — a command, a warning, an error, the final status. The list is
// virtualized and follows its end as lines come. The CMake panel shows the build of the scripts
// with it, the Export window — an export.
//
// A line that names a place in a file — an error of the compiler, a warning of cmake, a file
// that includes another (Panda::BuildOutputLocation) — shows the place as a link: a click opens
// the file there in the external editor of the editor settings.
class BuildLogView final : public PandaUI::Panel {
public:
    enum class LineKind { Command, Plain, Warning, Error, StatusOk, StatusFail };

    BuildLogView();

    // The folders a path of the output may be relative to: where the build runs, where its
    // cmake project is. For the lines that come from now on.
    void setFolders(std::vector<Panda::path_t> folders);
    void clear();
    void append(const std::vector<std::string> &lines); // each line is what its text says it is
    void append(std::string text, LineKind kind);
    size_t lineCount() const {
        return m_lines.size();
    }

    // What a line of the output is, by its text: a command starts with "> ", a diagnostic names itself.
    static LineKind classify(const std::string &text);

private:
    struct Line {
        std::string text;
        LineKind kind;
        std::optional<Panda::BuildOutputLocation> place; // the place in a file the line names
    };
    class Cell;

    void reload();

    std::vector<Line> m_lines;
    std::vector<Panda::path_t> m_folders;
    std::shared_ptr<PandaUI::CollectionView> m_list;
};

} // namespace Editor
