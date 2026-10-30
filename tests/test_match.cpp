#include "doctest/doctest.h"

#include <hyperfinder/match.h>

#include <string>
#include <vector>

using hyperfinder::globMatch;
using hyperfinder::isHiddenByName;
using hyperfinder::matchesAnyGlob;
using hyperfinder::matchesExtension;
using hyperfinder::normalizeExtension;
using hyperfinder::toLowerAscii;
using hyperfinder::utf8CodePointLength;

TEST_CASE("globMatch: literals must match exactly") {
    CHECK(globMatch("readme", "readme"));
    CHECK_FALSE(globMatch("readme", "readme.md"));
    CHECK_FALSE(globMatch("readme.md", "readme"));
    CHECK_FALSE(globMatch("Readme", "readme"));
}

TEST_CASE("globMatch: '*' matches zero or more characters") {
    CHECK(globMatch("*", ""));
    CHECK(globMatch("*", "anything"));
    CHECK(globMatch("*.txt", "a.txt"));
    CHECK(globMatch("*.txt", ".txt"));
    CHECK(globMatch("*.txt", "archive.tar.txt"));
    CHECK_FALSE(globMatch("*.txt", "a.txt.bak"));
    CHECK(globMatch("a*c", "abc"));
    CHECK(globMatch("a*c", "ac"));
    CHECK(globMatch("a*c", "aXYZc"));
    CHECK_FALSE(globMatch("a*c", "abcd"));
    CHECK(globMatch("a*b*c", "aXbYc"));
}

TEST_CASE("globMatch: '?' matches exactly one character") {
    CHECK(globMatch("a?c", "abc"));
    CHECK_FALSE(globMatch("a?c", "ac"));
    CHECK_FALSE(globMatch("a?c", "abbc"));
    CHECK(globMatch("???", "abc"));
    CHECK_FALSE(globMatch("???", "ab"));
    CHECK(globMatch("?.txt", "a.txt"));
}

TEST_CASE("globMatch: '?' counts code points, not bytes") {
    // "علی" is three two-byte code points, so "???" has to match it and "??"
    // must not. A byte-oriented implementation would get this wrong.
    CHECK(globMatch("???", "علی"));
    CHECK_FALSE(globMatch("??", "علی"));
    CHECK(globMatch("?", "ع"));
    CHECK_FALSE(globMatch("??", "ع"));

    // Three-byte code point and a four-byte emoji.
    CHECK(globMatch("?", "€"));
    CHECK(globMatch("?", "\xF0\x9F\x9A\x80"));  // U+1F680 rocket
    CHECK(globMatch("a?c", "a€c"));
}

TEST_CASE("globMatch: an empty pattern only matches an empty name") {
    CHECK(globMatch("", ""));
    CHECK_FALSE(globMatch("", "x"));
    CHECK_FALSE(globMatch("", "anything"));
}

TEST_CASE("globMatch: trailing stars match nothing") {
    CHECK(globMatch("abc*", "abc"));
    CHECK(globMatch("abc**", "abc"));
    CHECK(globMatch("**", ""));
    CHECK(globMatch("*abc*", "abc"));
}

TEST_CASE("globMatch: pathological patterns stay fast") {
    // A backtracking implementation without memoisation is exponential here.
    // This one is O(pattern * name), so the call returns immediately.
    const std::string name(200, 'a');
    CHECK_FALSE(globMatch("a*a*a*a*a*a*b", name));
    CHECK(globMatch("a*a*a*a*a*a*b", name + "b"));
}

TEST_CASE("globMatch: a '*' can span a separator, because names have none") {
    // Patterns are matched against the entry name, which never contains a
    // separator, so '*' behaves like it does in a shell's last path segment.
    CHECK(globMatch("*.tmp", "cache.tmp"));
    CHECK_FALSE(globMatch("*.tmp", "cache.tmp.old"));
}

TEST_CASE("utf8CodePointLength: reports the sequence length") {
    CHECK(utf8CodePointLength(static_cast<unsigned char>('a')) == 1);
    CHECK(utf8CodePointLength(static_cast<unsigned char>(0xD8)) == 2);  // Arabic
    CHECK(utf8CodePointLength(static_cast<unsigned char>(0xE2)) == 3);  // euro sign
    CHECK(utf8CodePointLength(static_cast<unsigned char>(0xF0)) == 4);  // emoji
    CHECK(utf8CodePointLength(static_cast<unsigned char>(0xFF)) == 1);  // invalid lead
}

TEST_CASE("toLowerAscii: folds ASCII and leaves UTF-8 alone") {
    CHECK(toLowerAscii("ABC.txt") == "abc.txt");
    CHECK(toLowerAscii("café") == "café");      // the é is untouched
    CHECK(toLowerAscii("CAFÉ") == "cafÉ");
    CHECK(toLowerAscii("علی") == "علی");
    CHECK(toLowerAscii("") == "");
}

TEST_CASE("isHiddenByName: a leading dot is hidden") {
    CHECK(isHiddenByName(".gitignore"));
    CHECK(isHiddenByName("."));
    CHECK_FALSE(isHiddenByName("gitignore"));
    CHECK_FALSE(isHiddenByName(""));
    CHECK_FALSE(isHiddenByName("a.b"));
}

TEST_CASE("normalizeExtension: leading dot and lower case") {
    CHECK(normalizeExtension("cpp") == ".cpp");
    CHECK(normalizeExtension(".cpp") == ".cpp");
    CHECK(normalizeExtension(".CPP") == ".cpp");
    CHECK(normalizeExtension("CPP") == ".cpp");
    CHECK(normalizeExtension("") == "");
}

TEST_CASE("matchesExtension: an empty filter accepts everything") {
    CHECK(matchesExtension("a.txt", {}));
    CHECK(matchesExtension("no-extension", {}));
}

TEST_CASE("matchesExtension: compares the last extension only") {
    const std::vector<std::string> gz{".gz"};
    CHECK(matchesExtension("archive.tar.gz", gz));
    CHECK_FALSE(matchesExtension("archive.tar", gz));
    CHECK_FALSE(matchesExtension("archive.gz.tar", gz));

    const std::vector<std::string> txt{".txt"};
    CHECK(matchesExtension("a.TXT", txt));
    CHECK(matchesExtension("a.txt", txt));
    CHECK_FALSE(matchesExtension("a.txt.bak", txt));
}

TEST_CASE("matchesExtension: a dotfile has no extension") {
    const std::vector<std::string> gitignore{".gitignore"};
    CHECK_FALSE(matchesExtension(".gitignore", gitignore));
    CHECK_FALSE(matchesExtension("no-dot", {".txt"}));
}

TEST_CASE("matchesAnyGlob: any matching pattern is enough") {
    const std::vector<std::string> patterns{"*.tmp", "build*", "*.log"};
    CHECK(matchesAnyGlob("x.tmp", patterns));
    CHECK(matchesAnyGlob("build-debug", patterns));
    CHECK(matchesAnyGlob("run.log", patterns));
    CHECK_FALSE(matchesAnyGlob("main.cpp", patterns));
    CHECK_FALSE(matchesAnyGlob("main.cpp", {}));
}
