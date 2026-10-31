#include <hyperfinder/json.h>

#include <cstddef>

namespace hyperfinder {
namespace {

// Length of a valid UTF-8 sequence starting at s[i], or 0 when the bytes there
// are not valid UTF-8: bad lead byte, bad continuation, overlong form, a
// surrogate, or a code point beyond U+10FFFF.
std::size_t utf8SequenceLength(std::string_view s, std::size_t i) {
    const unsigned char lead = static_cast<unsigned char>(s[i]);
    if (lead < 0x80) return 1;

    std::size_t length = 0;
    unsigned int min_codepoint = 0;
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        min_codepoint = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        min_codepoint = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        min_codepoint = 0x10000;
    } else {
        return 0;
    }
    if (i + length > s.size()) return 0;

    unsigned int codepoint = lead & (0xFFu >> (length + 1));
    for (std::size_t k = 1; k < length; ++k) {
        const unsigned char c = static_cast<unsigned char>(s[i + k]);
        if ((c & 0xC0) != 0x80) return 0;
        codepoint = (codepoint << 6) | (c & 0x3Fu);
    }
    if (codepoint < min_codepoint) return 0;
    if (codepoint > 0x10FFFF) return 0;
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return 0;
    return length;
}

}  // namespace

std::string jsonEscape(std::string_view value) {
    static const char kHex[] = "0123456789abcdef";

    std::string out;
    out.reserve(value.size() + 8);

    for (std::size_t i = 0; i < value.size();) {
        const unsigned char c = static_cast<unsigned char>(value[i]);

        switch (c) {
            case '"':
                out += "\\\"";
                ++i;
                continue;
            case '\\':
                out += "\\\\";
                ++i;
                continue;
            case '\b':
                out += "\\b";
                ++i;
                continue;
            case '\f':
                out += "\\f";
                ++i;
                continue;
            case '\n':
                out += "\\n";
                ++i;
                continue;
            case '\r':
                out += "\\r";
                ++i;
                continue;
            case '\t':
                out += "\\t";
                ++i;
                continue;
            default:
                break;
        }

        if (c < 0x20) {
            out += "\\u00";
            out += kHex[(c >> 4) & 0xF];
            out += kHex[c & 0xF];
            ++i;
            continue;
        }

        if (c < 0x80) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }

        const std::size_t length = utf8SequenceLength(value, i);
        if (length == 0) {
            // Invalid byte: emit U+FFFD and move on by one byte, so a malformed
            // sequence cannot swallow the bytes that follow it.
            out += "\\ufffd";
            ++i;
            continue;
        }
        out.append(value.data() + i, length);
        i += length;
    }

    return out;
}

}  // namespace hyperfinder
