// Smoke test of Fern/Files, in a folder of its own in the temporary one.
//
// Fern::writeFileAtomically: a new file holds the bytes as they are; an old one is replaced whole
// (no tail of a longer text stays); its permissions stay, a link stays a link (not on Windows); a
// read-only file, a folder in the place of the file and a folder that is not there are refused
// and nothing changes; no temporary file is left behind.
//
// Fern::FileWatcher, for both backends — the system's and the scan: a file written, made in a new
// folder, renamed and removed is told;
// a folder is told when it is made, renamed, moved in and moved away, and a file is told by the
// path its folder has now; a file beside what is watched is not told, nor one in a folder that
// was moved away; a watched file that is not there yet is told when it appears; nothing is told
// when nothing changed. Returns non-zero on failure.

#include <Fern/Files/AtomicWrite.hpp>
#include <Fern/Files/FileWatcher.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using path_t = std::filesystem::path;

namespace {

int g_failures = 0;

void check(bool condition, const std::string &what) {
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void write(const path_t &file, const std::string &text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out << text;
}

// A rename and a removal, tried again while something holds the file: on Windows a folder does
// not move while a file or a folder under it is open — and the scan opens them as it reads, a
// virus scanner as they appear.
void moveTo(const path_t &from, const path_t &to) {
    std::error_code error;
    for (int attempt = 0; attempt < 40; attempt++) {
        std::filesystem::rename(from, to, error);
        if (!error) { return; }
        std::this_thread::sleep_for(50ms);
    }
    check(false, "the test moves " + from.filename().string() + ": " + error.message());
}

void removeFile(const path_t &file) {
    std::error_code error;
    for (int attempt = 0; attempt < 40; attempt++) {
        std::filesystem::remove(file, error);
        if (!error) { return; }
        std::this_thread::sleep_for(50ms);
    }
    check(false, "the test removes " + file.filename().string() + ": " + error.message());
}

std::string list(const std::set<path_t> &paths) {
    std::string text;
    for (const path_t &path : paths) { text += (text.empty() ? "" : ", ") + path.generic_string(); }
    return text.empty() ? "nothing" : text;
}

// Takes from the watcher until every expected path was told, or the time is over. Answers with
// what was told.
std::set<path_t> collect(Fern::FileWatcher &watcher, const std::vector<path_t> &expected, std::chrono::milliseconds limit) {
    std::set<path_t> told;
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        Fern::FileWatcher::Changes changes;
        if (watcher.take(changes)) {
            told.insert(changes.paths.begin(), changes.paths.end());
            std::set<path_t> once(changes.paths.begin(), changes.paths.end());
            check(once.size() == changes.paths.size(), "a path is told once in a take");
            check(!changes.everything, "the watcher keeps track of a few files");
        }
        const bool all = std::all_of(expected.begin(), expected.end(), [&](const path_t &p) { return told.contains(p); });
        if (all && !expected.empty()) { break; }
        std::this_thread::sleep_for(20ms);
    }
    return told;
}

// Every one of the paths is told before the time is over.
void expectTold(
    Fern::FileWatcher &watcher, const std::vector<path_t> &expected, std::chrono::milliseconds limit, const std::string &what
) {
    const std::set<path_t> told = collect(watcher, expected, limit);
    const bool all = std::all_of(expected.begin(), expected.end(), [&](const path_t &p) { return told.contains(p); });
    check(all, what);
    if (!all) { std::printf("    told: %s\n", list(told).c_str()); }
}

// Nothing is told for this long.
void expectQuiet(Fern::FileWatcher &watcher, std::chrono::milliseconds time, const std::string &what) {
    const std::set<path_t> told = collect(watcher, {}, time);
    check(told.empty(), what);
    if (!told.empty()) { std::printf("    told: %s\n", list(told).c_str()); }
}

void run(Fern::FileWatcher::Backend backend, const char *name, const path_t &base, std::chrono::milliseconds limit) {
    std::printf("%s\n", name);
    std::error_code error;
    std::filesystem::create_directories(base / "watched");
    std::filesystem::create_directories(base / "beside");
    std::filesystem::create_directories(base / "single");
    write(base / "watched" / "a.txt", "a");
    // The watcher tells real paths: the temporary folder of macOS is a link, that of Windows may
    // be written with a short name.
    const path_t root = std::filesystem::weakly_canonical(base, error);
    const path_t watched = root / "watched";
    const path_t a = watched / "a.txt", b = watched / "sub" / "b.txt", c = watched / "c.txt";
    const path_t one = root / "single" / "one.txt";

    Fern::FileWatcher watcher({base / "watched", base / "single" / "one.txt"}, backend);
    // What was written a moment before the watch began may be told (the system gathers events
    // for a while): it is taken away, and from then on nothing is told by itself.
    collect(watcher, {}, 700ms);
    expectQuiet(watcher, 500ms, "nothing is told while nothing changes");

    write(base / "watched" / "a.txt", "more");
    expectTold(watcher, {a}, limit, "a file written is told");

    write(base / "watched" / "sub" / "b.txt", "b");
    expectTold(watcher, {b}, limit, "a file made in a new folder is told");

    write(base / "watched" / "n1" / "n2" / "n3" / "n.txt", "n");
    expectTold(watcher, {watched / "n1" / "n2" / "n3" / "n.txt"}, limit, "a file made in new folders, one in another, is told");

    moveTo(base / "watched" / "a.txt", base / "watched" / "c.txt");
    expectTold(watcher, {a, c}, limit, "a file renamed is told by both names");

    removeFile(base / "watched" / "sub" / "b.txt");
    expectTold(watcher, {b}, limit, "a file removed is told");

    // Folders: told as themselves when they appear and when they go, and the watch stays with
    // the tree as it is now.
    std::filesystem::create_directories(base / "watched" / "sub" / "deep", error);
    expectTold(watcher, {watched / "sub" / "deep"}, limit, "a folder made is told");

    moveTo(base / "watched" / "sub", base / "watched" / "moved");
    expectTold(watcher, {watched / "sub", watched / "moved"}, limit, "a folder renamed is told by both names");

    write(base / "watched" / "moved" / "deep" / "d.txt", "d");
    expectTold(
        watcher, {watched / "moved" / "deep" / "d.txt"}, limit, "a file made in a folder that was renamed is told by its new path"
    );

    write(base / "beside" / "in" / "e.txt", "e");
    moveTo(base / "beside" / "in", base / "watched" / "in");
    expectTold(watcher, {watched / "in"}, limit, "a folder moved in is told");

    write(base / "watched" / "in" / "f.txt", "f");
    expectTold(watcher, {watched / "in" / "f.txt"}, limit, "a file made in a folder that was moved in is told");

    moveTo(base / "watched" / "in", base / "beside" / "out");
    expectTold(watcher, {watched / "in"}, limit, "a folder moved away is told");
    collect(watcher, {}, 500ms); // what it held may be told after it

    write(base / "beside" / "out" / "g.txt", "g");
    write(base / "beside" / "x.txt", "x");
    write(base / "single" / "other.txt", "other");
    expectQuiet(watcher, limit / 2, "a file beside what is watched is not told, nor one in a folder that was moved away");

    write(base / "single" / "one.txt", "one");
    expectTold(watcher, {one}, limit, "a watched file that was not there is told when it appears");

    expectQuiet(watcher, limit / 2, "nothing more is told when nothing changes");
}

std::string readAll(const path_t &file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void atomicWrite(const path_t &base) {
    using perms = std::filesystem::perms;
    std::printf("the atomic write\n");
    std::error_code error;
    std::filesystem::create_directories(base, error);
    std::set<std::string> made; // what the test leaves in the folder on purpose
    std::string why;

    const path_t file = base / "world.world";
    made.insert(file.filename().string());
    check(Fern::writeFileAtomically(file, "the first version, the longer one", &why), "a new file is written: " + why);
    check(readAll(file) == "the first version, the longer one", "a new file holds the bytes");
    check(Fern::writeFileAtomically(file, "second", &why), "a file is written again: " + why);
    check(readAll(file) == "second", "the new bytes replace the old ones whole: \"" + readAll(file) + "\"");
    const std::string binary("a\0b\r\n\xff", 6);
    check(Fern::writeFileAtomically(file, binary, &why) && readAll(file) == binary, "the bytes are written as they are");
    check(Fern::writeFileAtomically(file, "", &why) && std::filesystem::file_size(file, error) == 0, "no bytes make an empty file");

#ifndef _WIN32
    const perms mode = perms::owner_read | perms::owner_write | perms::group_read;
    std::filesystem::permissions(file, mode, error);
    check(Fern::writeFileAtomically(file, "third", &why), "a file with permissions of its own is written: " + why);
    check((std::filesystem::status(file, error).permissions() & perms::mask) == mode, "the permissions of the old file stay");

    const path_t link = base / "link.world";
    std::filesystem::create_symlink(file.filename(), link, error);
    if (!error) {
        made.insert(link.filename().string());
        check(Fern::writeFileAtomically(link, "through the link", &why), "a file is written through a link: " + why);
        check(std::filesystem::is_symlink(std::filesystem::symlink_status(link, error)), "a link stays a link");
        check(readAll(file) == "through the link", "the file a link points to gets the bytes");
    } else {
        check(false, "the test makes a link: " + error.message());
    }
#endif

    const path_t locked = base / "locked.world";
    made.insert(locked.filename().string());
    check(Fern::writeFileAtomically(locked, "kept", &why), "the file to lock is written: " + why);
    std::filesystem::permissions(locked, perms::owner_write | perms::group_write | perms::others_write, std::filesystem::perm_options::remove, error);
    why.clear();
    check(!Fern::writeFileAtomically(locked, "changed", &why) && !why.empty(), "a read-only file is refused, and the error says why");
    check(readAll(locked) == "kept", "a read-only file stays as it was: \"" + readAll(locked) + "\"");
    std::filesystem::permissions(locked, perms::owner_write, std::filesystem::perm_options::add, error); // to be removed

    const path_t folder = base / "folder.world";
    made.insert(folder.filename().string());
    std::filesystem::create_directories(folder / "inside", error);
    check(!Fern::writeFileAtomically(folder, "x", &why), "a folder in the place of the file is not written over");
    check(std::filesystem::is_directory(folder / "inside", error), "the folder stays as it was");

    why.clear();
    check(!Fern::writeFileAtomically(base / "missing" / "file.world", "x", &why) && !why.empty(), "a file in a folder that is not there is refused");
    check(!std::filesystem::exists(base / "missing", error), "a folder that is not there is not made");

    std::string strays;
    for (const auto &entry : std::filesystem::directory_iterator(base, error)) {
        if (made.count(entry.path().filename().string()) == 0) { strays += " " + entry.path().filename().string(); }
    }
    check(strays.empty(), "no temporary file is left:" + strays);
}

} // namespace

int main() {
    std::error_code error;
    const path_t base = std::filesystem::temp_directory_path() /
                        ("fern-files-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(base, error);
    atomicWrite(base / "atomic");
    run(Fern::FileWatcher::Backend::System, "the backend of the system", base / "system", 3000ms);
    run(Fern::FileWatcher::Backend::Scan, "the scan", base / "scan", 4000ms);
    std::filesystem::remove_all(base, error);
    if (g_failures != 0) {
        std::printf("FAILED (%d failure(s))\n", g_failures);
        return 1;
    }
    std::printf("PASSED\n");
    return 0;
}
