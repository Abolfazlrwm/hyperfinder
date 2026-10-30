#ifndef HYPERFINDER_MATCH_H
#define HYPERFINDER_MATCH_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hyperfinder {

// ASCII-only lower-casing, byte by byte.
//
// Case folding stops at ASCII on purpose: the file systems this tool targets
// do not define Unicode case folding, doing it properly needs ICU, and a
// half-correct Unicode fold would silently match the wrong names. Bytes above
// 0x7F are left untouched, so UTF-8 sequences survive intact.
std::string toLowerAscii(std::string_view value);

// Wildcard match used by --exclude: '*' matches zero or more characters, '?'
// matches exactly one character, everything else matches literally.
//
// Iterative, with backtracking on the last '*'. Worst case is O(pattern *
// name) but never exponential, so patterns like "a*a*a*a*b" stay cheap.
//
// '?' consumes one UTF-8 code point, not one byte, so "?" matches "ع" (two
// bytes) exactly once. A malformed sequence is treated as a single byte, which
// keeps the function total: it never reads past the end of the input.
bool globMatch(std::string_view pattern, std::string_view name);

// True when the name starts with a dot. This is the POSIX definition; on
// Windows the hidden attribute is checked as well, in the searcher, because
// std::filesystem does not expose it.
bool isHiddenByName(std::string_view name);

// Normalises an extension to a leading dot and lower case: "cpp" and ".CPP"
// both become ".cpp".
std::string normalizeExtension(std::string_view ext);

// True when the last extension of `name` matches any entry of `extensions`.
// An empty `extensions` matches everything. "archive.tar.gz" has the extension
// ".gz", because only the last dot counts.
bool matchesExtension(std::string_view name, const std::vector<std::string>& extensions);

// True when `name` matches any of the glob patterns.
bool matchesAnyGlob(std::string_view name, const std::vector<std::string>& patterns);

// Byte length of the UTF-8 code point starting at `lead`. Returns 1 for an
// invalid lead byte so callers can always make progress.
std::size_t utf8CodePointLength(unsigned char lead);

}  // namespace hyperfinder

#endif  // HYPERFINDER_MATCH_H
