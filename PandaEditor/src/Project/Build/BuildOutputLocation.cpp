#include "Project/Build/BuildOutputLocation.hpp"

#include <string>
#include <system_error>

namespace Panda {

namespace {

    // A line of a build may be the whole command of a compiler: a place is looked for at its
    // start, and the file system is asked a few times a line, not for every word of it.
    constexpr size_t SCANNED = 2000;
    constexpr int MOST_MARKS = 4;
    constexpr int MOST_PATHS = 12;

    bool isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    bool isBlank(char c) {
        return c == ' ' || c == '\t';
    }

    bool isLetterOrDigit(char c) {
        return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    // The number written at `at`, which moves past it; 0 — none, or one too long for a line.
    int readNumber(std::string_view text, size_t &at) {
        size_t i = at;
        int value = 0;
        while (i < text.size() && isDigit(text[i]) && i - at < 8) {
            value = value * 10 + (text[i] - '0');
            i++;
        }
        if (i == at || (i < text.size() && isDigit(text[i]))) { return 0; }
        at = i;
        return value;
    }

    bool isAbsolute(std::string_view written) {
        if (written.empty()) { return false; }
        if (written[0] == '/' || written[0] == '\\') { return true; }
        return written.size() > 2 && written[1] == ':' && (written[2] == '\\' || written[2] == '/'); // C:\…
    }

    // What may be the path of a source file: an absolute path, or a name with an extension (the
    // words of a message are neither, and the file system is not asked about them).
    bool looksLikePath(std::string_view written) {
        if (written.empty() || isBlank(written.back())) { return false; }
        if (isAbsolute(written)) { return true; }
        const size_t name = written.find_last_of("/\\");
        const std::string_view file = name == std::string_view::npos ? written : written.substr(name + 1);
        const size_t dot = file.find_last_of('.');
        return dot != std::string_view::npos && dot > 0 && dot + 1 < file.size();
    }

    // The file a path of the output means, if it is there.
    std::optional<path_t> existingFile(std::string_view written, const std::vector<path_t> &folders) {
        std::error_code error;
        const path_t path{std::string(written)};
        if (isAbsolute(written)) {
            if (std::filesystem::is_regular_file(path, error)) { return path.lexically_normal(); }
            return std::nullopt;
        }
        for (const path_t &folder : folders) {
            const path_t candidate = folder / path;
            if (std::filesystem::is_regular_file(candidate, error)) { return candidate.lexically_normal(); }
        }
        return std::nullopt;
    }

} // namespace

std::optional<BuildOutputLocation> findBuildOutputLocation(std::string_view text, const std::vector<path_t> &folders) {
    const size_t limit = text.size() < SCANNED ? text.size() : SCANNED;
    int marks = 0;
    for (size_t at = 1; at + 1 < limit && marks < MOST_MARKS; at++) {
        // A place ends with the number of its line: ":12", ":12:5", "(12)", "(12,5)" — right after
        // the path, with no blank between.
        const char mark = text[at];
        if ((mark != ':' && mark != '(') || !isDigit(text[at + 1])) { continue; }
        if (isBlank(text[at - 1]) || text[at - 1] == ':' || text[at - 1] == '(') { continue; }
        size_t end = at + 1;
        const int line = readNumber(text, end);
        if (line <= 0) { continue; }
        int column = 0;
        if (mark == ':') {
            if (end + 1 < text.size() && text[end] == ':' && isDigit(text[end + 1])) {
                size_t after = end + 1;
                const int number = readNumber(text, after);
                if (number > 0) {
                    column = number;
                    end = after;
                }
            }
            if (end < text.size() && isLetterOrDigit(text[end])) { continue; } // "name.cpp:12abc"
        } else {
            if (end < text.size() && text[end] == ',') {
                size_t after = end + 1;
                column = readNumber(text, after);
                if (column <= 0) { continue; }
                end = after;
            }
            if (end >= text.size() || text[end] != ')') { continue; }
            end++;
        }
        marks++;
        // The path stands before the mark. It may hold blanks, and words may stand before it
        // ("In file included from …"): it is tried from every word on, the longest first.
        size_t start = 0;
        for (int tried = 0; tried < MOST_PATHS && start < at;) {
            while (start < at && isBlank(text[start])) { start++; }
            if (start >= at) { break; }
            const std::string_view written = text.substr(start, at - start);
            if (looksLikePath(written)) {
                tried++;
                if (std::optional<path_t> file = existingFile(written, folders)) {
                    BuildOutputLocation location;
                    location.file = std::move(*file);
                    location.line = line;
                    location.column = column;
                    location.begin = start;
                    location.end = end;
                    return location;
                }
            }
            const size_t blank = text.find_first_of(" \t", start);
            if (blank == std::string_view::npos || blank >= at) { break; }
            start = blank + 1;
        }
    }
    return std::nullopt;
}

} // namespace Panda
