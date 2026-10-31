#include "doctest/doctest.h"

#include <hyperfinder/json.h>

#include <string>

using hyperfinder::jsonEscape;

TEST_CASE("jsonEscape leaves plain ASCII unchanged") {
    CHECK(jsonEscape("hello world") == "hello world");
    CHECK(jsonEscape("") == "");
}

TEST_CASE("jsonEscape escapes quotes and backslashes") {
    CHECK(jsonEscape("a\"b") == "a\\\"b");
    CHECK(jsonEscape("a\\b") == "a\\\\b");
}

TEST_CASE("jsonEscape escapes control characters") {
    CHECK(jsonEscape("a\nb") == "a\\nb");
    CHECK(jsonEscape("a\rb") == "a\\rb");
    CHECK(jsonEscape("a\tb") == "a\\tb");
    // A control character without a short escape uses \u00XX.
    CHECK(jsonEscape(std::string("a\x01" "b", 3)) == "a\\u0001b");
}

TEST_CASE("jsonEscape leaves valid UTF-8 unchanged") {
    CHECK(jsonEscape("سلام") == "سلام");
    CHECK(jsonEscape("café") == "café");
}

TEST_CASE("jsonEscape replaces an invalid byte with U+FFFD") {
    // 0xFF is never a valid UTF-8 lead byte.
    CHECK(jsonEscape(std::string("a\xff" "b", 3)) == "a\\ufffdb");
}

TEST_CASE("jsonEscape rejects an overlong encoding") {
    // 0xC0 0x80 is an overlong encoding of NUL; each byte is invalid on its own.
    CHECK(jsonEscape(std::string("\xc0\x80", 2)) == "\\ufffd\\ufffd");
}

TEST_CASE("jsonEscape rejects a lone continuation byte") {
    CHECK(jsonEscape(std::string("\x80", 1)) == "\\ufffd");
}

TEST_CASE("jsonEscape rejects a surrogate code point") {
    // U+D800 encoded as three bytes is not allowed in UTF-8.
    CHECK(jsonEscape(std::string("\xed\xa0\x80", 3)) == "\\ufffd\\ufffd\\ufffd");
}
