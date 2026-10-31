#ifndef HYPERFINDER_OUTPUT_H
#define HYPERFINDER_OUTPUT_H

#include <cstddef>
#include <string>
#include <vector>

#include <hyperfinder/file_searcher.h>
#include <hyperfinder/options.h>

namespace hyperfinder {

// Prepares the Windows console for UTF-8 output. A no-op on other platforms,
// and best effort on Windows: if it fails, non-ASCII names may not render but
// nothing else breaks.
void prepareConsole();

// Turns ColorMode into a yes/no. Never is always off, Always is on (subject to
// the terminal accepting escapes), Auto needs a TTY and no NO_COLOR.
bool resolveColor(ColorMode mode);

// Writes one path per line to stdout through a single buffered write. When
// colour is on, only the matched part of the file name is highlighted.
void writeMatches(const std::vector<std::string>& matches, const Options& options, bool color);

// Writes one "path:line:text" record per content match, in the order given.
// When colour is on, the matched text inside the line is highlighted.
void writeContentMatches(const std::vector<ContentMatch>& matches, const Options& options,
                         bool color);

// Writes one JSON object per line (NDJSON): {"path":...} for name matches, or
// {"path":...,"line":N,"text":...} for content matches. Never coloured.
void writeJsonMatches(const std::vector<std::string>& paths,
                      const std::vector<ContentMatch>& content_matches, bool content_mode);

// Writes records terminated by a NUL byte instead of a newline. The record is
// the path for name matches, or "path:line:text" for content matches.
void writePrint0Matches(const std::vector<std::string>& paths,
                        const std::vector<ContentMatch>& content_matches, bool content_mode);

// Writes the warning (if any) and the summary to stderr.
void writeSummary(const SearchStats& stats, std::size_t match_count);

}  // namespace hyperfinder

#endif  // HYPERFINDER_OUTPUT_H
