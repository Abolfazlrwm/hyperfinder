#include <hyperfinder/match.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace hyperfinder {
namespace {

constexpr std::size_t kNoStar = static_cast<std::size_t>(-1);

}  // namespace

std::string toLowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        // Bytes >= 0x80 belong to a UTF-8 sequence and must not be folded.
        return c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    });
    return result;
}

std::size_t utf8CodePointLength(unsigned char lead) {
    if (lead < 0x80) return 1;                 // ASCII
    if ((lead & 0xE0) == 0xC0) return 2;       // 110xxxxx
    if ((lead & 0xF0) == 0xE0) return 3;       // 1110xxxx
    if ((lead & 0xF8) == 0xF0) return 4;       // 11110xxx
    return 1;                                  // invalid lead: consume one byte
}

bool globMatch(std::string_view pattern, std::string_view name) {
    std::size_t p = 0;                            // read position in pattern
    std::size_t n = 0;                            // read position in name
    std::size_t star_p = kNoStar;                 // position of the last '*'
    std::size_t star_n = 0;                       // name position it has consumed up to

    while (n < name.size()) {
        if (p < pattern.size()) {
            const char token = pattern[p];

            if (token == '*') {
                // Remember the star and try to match zero characters first.
                star_p = p;
                star_n = n;
                ++p;
                continue;
            }

            if (token == '?') {
                p += 1;
                n += utf8CodePointLength(static_cast<unsigned char>(name[n]));
                continue;
            }

            // Literal: compare whole code points so that a multi-byte character
            // is either fully equal or not equal at all.
            const std::size_t pattern_length =
                utf8CodePointLength(static_cast<unsigned char>(token));
            const std::size_t name_length =
                utf8CodePointLength(static_cast<unsigned char>(name[n]));
            if (pattern_length == name_length &&
                pattern.compare(p, pattern_length, name, n, name_length) == 0) {
                p += pattern_length;
                n += name_length;
                continue;
            }
        }

        if (star_p != kNoStar) {
            // Backtrack: let the star swallow one more code point.
            star_n += utf8CodePointLength(static_cast<unsigned char>(name[star_n]));
            n = star_n;
            p = star_p + 1;
            continue;
        }

        return false;
    }

    // Trailing stars in the pattern can match the empty remainder.
    while (p < pattern.size() && pattern[p] == '*') ++p;

    return p == pattern.size();
}

bool isHiddenByName(std::string_view name) {
    return !name.empty() && name.front() == '.';
}

std::string normalizeExtension(std::string_view ext) {
    if (ext.empty()) return {};
    std::string normalized(ext);
    if (normalized.front() != '.') normalized.insert(normalized.begin(), '.');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    });
    return normalized;
}

bool matchesExtension(std::string_view name, const std::vector<std::string>& extensions) {
    if (extensions.empty()) return true;

    // A leading dot with no other dot means a dotfile such as ".gitignore",
    // which has no extension.
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return false;

    const std::string extension = toLowerAscii(name.substr(dot));
    for (const std::string& wanted : extensions) {
        if (extension == wanted) return true;
    }
    return false;
}

bool matchesAnyGlob(std::string_view name, const std::vector<std::string>& patterns) {
    for (const std::string& pattern : patterns) {
        if (globMatch(pattern, name)) return true;
    }
    return false;
}

}  // namespace hyperfinder
