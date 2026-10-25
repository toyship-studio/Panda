#include "Project/Plugins/Semver.hpp"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace Panda {

namespace {

    std::string_view trim(std::string_view text) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) { text.remove_prefix(1); }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) { text.remove_suffix(1); }
        return text;
    }

    bool parseNumber(std::string_view text, int &out) {
        if (text.empty() || text.size() > 9) { return false; }
        int value = 0;
        for (char c : text) {
            if (c < '0' || c > '9') { return false; }
            value = value * 10 + (c - '0');
        }
        out = value;
        return true;
    }

    bool isWildcard(std::string_view part) {
        return part == "x" || part == "X" || part == "*";
    }

    // A version in a range: 1..3 numeric parts, the missing or wildcard ones are open.
    struct Partial {
        int parts[3] = {0, 0, 0};
        int count = 0; // how many parts are fixed
        bool any = false;
    };

    std::optional<Partial> parsePartial(std::string_view text) {
        Partial partial;
        if (text.empty() || isWildcard(text)) {
            partial.any = true;
            return partial;
        }
        // A pre-release or build tag on a range bound is ignored (the bound is the core).
        const size_t dash = text.find('-');
        if (dash != std::string_view::npos) { text = text.substr(0, dash); }
        const size_t plus = text.find('+');
        if (plus != std::string_view::npos) { text = text.substr(0, plus); }
        size_t begin = 0;
        while (partial.count < 3) {
            const size_t dot = text.find('.', begin);
            const std::string_view part = text.substr(begin, dot == std::string_view::npos ? std::string_view::npos : dot - begin);
            if (isWildcard(part)) { break; }
            if (!parseNumber(part, partial.parts[partial.count])) { return std::nullopt; }
            ++partial.count;
            if (dot == std::string_view::npos) { break; }
            begin = dot + 1;
        }
        if (partial.count == 0) { partial.any = true; }
        return partial;
    }

    Semver core(int major, int minor, int patch) {
        Semver v;
        v.major = major;
        v.minor = minor;
        v.patch = patch;
        return v;
    }

    struct Bound {
        Semver version;
        bool inclusive = true;
    };

    // One comparator set: lower <= v < upper, either side optional.
    struct Interval {
        std::optional<Bound> lower;
        std::optional<Bound> upper;

        bool contains(const Semver &v) const {
            if (lower) {
                const int cmp = v.compareCore(lower->version);
                if (cmp < 0 || (cmp == 0 && !lower->inclusive)) { return false; }
            }
            if (upper) {
                const int cmp = v.compareCore(upper->version);
                if (cmp > 0 || (cmp == 0 && !upper->inclusive)) { return false; }
            }
            return true;
        }
    };

    // Applies one comparator to the interval; false — a syntax error.
    bool applyComparator(std::string_view token, Interval &interval) {
        std::string_view op;
        for (std::string_view candidate : {">=", "<=", ">", "<", "=", "^", "~"}) {
            if (token.substr(0, candidate.size()) == candidate) {
                op = candidate;
                break;
            }
        }
        const std::optional<Partial> partial = parsePartial(trim(token.substr(op.size())));
        if (!partial) { return false; }
        const Partial &p = *partial;
        auto tighten = [&interval](std::optional<Bound> lower, std::optional<Bound> upper) {
            if (lower && (!interval.lower || lower->version.compareCore(interval.lower->version) >= 0)) { interval.lower = lower; }
            if (upper && (!interval.upper || upper->version.compareCore(interval.upper->version) <= 0)) { interval.upper = upper; }
        };
        if (p.any) { return op.empty() || op == ">=" || op == "<=" || op == "=" || op == "^" || op == "~"; }
        const Semver low = core(p.parts[0], p.parts[1], p.parts[2]);
        // The next version after the fixed parts: 1.2 → 1.3.0, 1 → 2.0.0.
        const Semver next = p.count == 1 ? core(p.parts[0] + 1, 0, 0) : p.count == 2 ? core(p.parts[0], p.parts[1] + 1, 0) : core(p.parts[0], p.parts[1], p.parts[2] + 1);
        if (op == "^") {
            Semver upper;
            if (p.parts[0] > 0 || p.count == 1) {
                upper = core(p.parts[0] + 1, 0, 0);
            } else if (p.parts[1] > 0 || p.count == 2) {
                upper = core(0, p.parts[1] + 1, 0);
            } else {
                upper = core(0, 0, p.parts[2] + 1);
            }
            tighten(Bound{low, true}, Bound{upper, false});
        } else if (op == "~") {
            tighten(Bound{low, true}, Bound{p.count == 1 ? core(p.parts[0] + 1, 0, 0) : core(p.parts[0], p.parts[1] + 1, 0), false});
        } else if (op == ">=") {
            tighten(Bound{low, true}, std::nullopt);
        } else if (op == ">") {
            // > 1.2 means at least 1.3.0; > 1.2.3 — more than 1.2.3.
            if (p.count == 3) {
                tighten(Bound{low, false}, std::nullopt);
            } else {
                tighten(Bound{next, true}, std::nullopt);
            }
        } else if (op == "<") {
            tighten(std::nullopt, Bound{low, false});
        } else if (op == "<=") {
            // <= 1.2 means anything below 1.3.0.
            if (p.count == 3) {
                tighten(std::nullopt, Bound{low, true});
            } else {
                tighten(std::nullopt, Bound{next, false});
            }
        } else { // "=" or nothing: exact, or the whole family of a partial version
            if (p.count == 3) {
                tighten(Bound{low, true}, Bound{low, true});
            } else {
                tighten(Bound{low, true}, Bound{next, false});
            }
        }
        return true;
    }

    std::vector<std::string_view> split(std::string_view text, std::string_view separator) {
        std::vector<std::string_view> parts;
        size_t begin = 0;
        while (true) {
            const size_t at = text.find(separator, begin);
            parts.push_back(text.substr(begin, at == std::string_view::npos ? std::string_view::npos : at - begin));
            if (at == std::string_view::npos) { break; }
            begin = at + separator.size();
        }
        return parts;
    }

    // Pre-release tags: numeric identifiers compare as numbers and go before alphanumeric ones.
    int comparePrerelease(std::string_view a, std::string_view b) {
        if (a.empty() && b.empty()) { return 0; }
        if (a.empty()) { return 1; } // a release is newer than its pre-releases
        if (b.empty()) { return -1; }
        const std::vector<std::string_view> as = split(a, "."), bs = split(b, ".");
        for (size_t i = 0; i < as.size() && i < bs.size(); ++i) {
            int an = 0, bn = 0;
            const bool aNum = parseNumber(as[i], an), bNum = parseNumber(bs[i], bn);
            if (aNum && bNum) {
                if (an != bn) { return an < bn ? -1 : 1; }
            } else if (aNum != bNum) {
                return aNum ? -1 : 1;
            } else {
                const int cmp = as[i].compare(bs[i]);
                if (cmp != 0) { return cmp < 0 ? -1 : 1; }
            }
        }
        if (as.size() == bs.size()) { return 0; }
        return as.size() < bs.size() ? -1 : 1;
    }

} // namespace

std::optional<Semver> Semver::parse(std::string_view text) {
    text = trim(text);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) { text.remove_prefix(1); }
    Semver v;
    const size_t plus = text.find('+');
    if (plus != std::string_view::npos) { text = text.substr(0, plus); }
    const size_t dash = text.find('-');
    if (dash != std::string_view::npos) {
        v.prerelease = std::string(text.substr(dash + 1));
        text = text.substr(0, dash);
    }
    const std::vector<std::string_view> parts = split(text, ".");
    if (parts.size() != 3) { return std::nullopt; }
    if (!parseNumber(parts[0], v.major) || !parseNumber(parts[1], v.minor) || !parseNumber(parts[2], v.patch)) { return std::nullopt; }
    return v;
}

int Semver::compareCore(const Semver &other) const {
    if (major != other.major) { return major < other.major ? -1 : 1; }
    if (minor != other.minor) { return minor < other.minor ? -1 : 1; }
    if (patch != other.patch) { return patch < other.patch ? -1 : 1; }
    return 0;
}

int Semver::compare(const Semver &other) const {
    const int cmp = compareCore(other);
    if (cmp != 0) { return cmp; }
    return comparePrerelease(prerelease, other.prerelease);
}

std::string Semver::toString() const {
    std::string text = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) { text += "-" + prerelease; }
    return text;
}

bool semverSatisfies(const Semver &version, std::string_view range) {
    range = trim(range);
    if (range.empty()) { return false; }
    for (std::string_view alternative : split(range, "||")) {
        alternative = trim(alternative);
        Interval interval;
        bool valid = true;
        if (!alternative.empty() && alternative != "*") {
            // Comparators are separated by spaces; an operator may be followed by a space ("> 1.0").
            std::vector<std::string_view> tokens;
            size_t begin = 0;
            while (begin < alternative.size()) {
                while (begin < alternative.size() && std::isspace(static_cast<unsigned char>(alternative[begin]))) { ++begin; }
                if (begin >= alternative.size()) { break; }
                size_t end = begin;
                while (end < alternative.size() && !std::isspace(static_cast<unsigned char>(alternative[end]))) { ++end; }
                std::string_view token = alternative.substr(begin, end - begin);
                begin = end;
                const bool bareOperator = token == ">=" || token == "<=" || token == ">" || token == "<" || token == "=" || token == "^" || token == "~";
                if (bareOperator) {
                    while (begin < alternative.size() && std::isspace(static_cast<unsigned char>(alternative[begin]))) { ++begin; }
                    end = begin;
                    while (end < alternative.size() && !std::isspace(static_cast<unsigned char>(alternative[end]))) { ++end; }
                    // The next token joins the operator (a string_view over the original text).
                    token = alternative.substr(token.data() - alternative.data(), end - (token.data() - alternative.data()));
                    begin = end;
                }
                tokens.push_back(token);
            }
            for (std::string_view token : tokens) {
                // Remove the inner spaces of "> 1.0" before parsing.
                std::string compact;
                for (char c : token) {
                    if (!std::isspace(static_cast<unsigned char>(c))) { compact += c; }
                }
                if (!applyComparator(compact, interval)) {
                    valid = false;
                    break;
                }
            }
        }
        if (valid && interval.contains(version)) { return true; }
    }
    return false;
}

bool semverSatisfies(std::string_view version, std::string_view range) {
    const std::optional<Semver> parsed = Semver::parse(version);
    return parsed && semverSatisfies(*parsed, range);
}

int semverCompare(std::string_view a, std::string_view b) {
    const std::optional<Semver> va = Semver::parse(a), vb = Semver::parse(b);
    if (va && vb) { return va->compare(*vb); }
    if (va) { return 1; }
    if (vb) { return -1; }
    const int cmp = a.compare(b);
    return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

} // namespace Panda
