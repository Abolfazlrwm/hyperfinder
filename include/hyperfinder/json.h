#ifndef HYPERFINDER_JSON_H
#define HYPERFINDER_JSON_H

#include <string>
#include <string_view>

namespace hyperfinder {

// Escapes a string for inclusion between double quotes in a JSON document.
//
// The result is a valid JSON string body:
//   * '"' and '\' are backslash-escaped;
//   * the control characters below 0x20 use the short escapes \n \r \t \b \f
//     where they exist and \u00XX otherwise;
//   * bytes that are not part of a valid UTF-8 sequence are replaced by U+FFFD,
//     so the output is always valid UTF-8 even for a malformed file name.
//
// Valid UTF-8 is copied through unchanged.
std::string jsonEscape(std::string_view value);

}  // namespace hyperfinder

#endif  // HYPERFINDER_JSON_H
