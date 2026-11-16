#pragma once

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// The output of a build as a list of lines: what cmake and the cook of a game said, each line
// colored by what it is — a command, a warning, an error, the final status. The list is
// virtualized and follows its end as lines come. The CMake panel shows the build of the scripts
// with it, the Export window — an export.
class BuildLogView final : public PandaUI::Panel {
public:
    enum class LineKind { Command, Plain, Warning, Error, StatusOk, StatusFail };

    BuildLogView();

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
    };
    class Cell;

    void reload();

    std::vector<Line> m_lines;
    std::shared_ptr<PandaUI::CollectionView> m_list;
};

} // namespace Editor
