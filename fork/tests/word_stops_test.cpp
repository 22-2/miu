// Unit tests for fork/WordStops.h. Built and run by the "test" job in build-windows.yml:
//   cl /std:c++17 /EHsc /W4 /WX fork\tests\word_stops_test.cpp
// Japanese text is written as UTF-8 escapes so this file stays ASCII.
#include "../WordStops.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// Run boundaries as "start-end" pairs, with an "s" for runs of spaces.
static std::string show(const std::vector<forkword::Run>& runs) {
    std::string out;
    for (const forkword::Run& r : runs) out += std::to_string(r.start) + "-" + std::to_string(r.end) + (r.space ? "s " : " ");
    return out;
}

static void checkRuns(const std::string& text, const std::vector<bool>* dict, const std::string& expected, int line) {
    std::string actual = show(forkword::runs(forkword::decode(text), dict));
    if (actual == expected) return;
    std::printf("FAIL %s:%d: runs\n  actual:   %s\n  expected: %s\n", __FILE__, line, actual.c_str(), expected.c_str());
    ++failures;
}
#define CHECK_RUNS(text, dict, expected) checkRuns((text), (dict), (expected), __LINE__)

// A dictionary result with boundaries at the given UTF-16 offsets (and at both ends).
static std::vector<bool> dictAt(const std::string& text, std::vector<size_t> offsets) {
    size_t n = forkword::decode(text).utf16.size();
    std::vector<bool> flags(n + 1, false);
    flags[0] = flags[n] = true;
    for (size_t o : offsets) flags[o] = true;
    return flags;
}

static const char* const kNihongo = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";   // nihongo (3 kanji)
static const char* const kNo = "\xE3\x81\xAE";                               // no (hiragana)
static const char* const kTekisuto = "\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88"; // tekisuto (4 katakana)
static const char* const kDesu = "\xE3\x81\xA7\xE3\x81\x99";                 // desu (2 hiragana)
static const char* const kMaru = "\xE3\x80\x82";                             // ideographic full stop
static const char* const kKagi = "\xE3\x80\x8D";                             // closing corner bracket
static const char* const kWideSpace = "\xE3\x80\x80";                        // ideographic space
static const char* const kTokyoTocho = "\xE6\x9D\xB1\xE4\xBA\xAC\xE9\x83\xBD\xE5\xBA\x81"; // 4 kanji
static const char* const kSugoi = "\xE3\x81\x99\xE3\x81\x94\xE3\x83\xBC\xE3\x81\x84"; // su go (long vowel) i

static void testDecode() {
    forkword::Decoded d = forkword::decode(std::string("a\xC3\xA9") + "\xF0\xA0\xAE\xB7" + "b");  // a, e-acute, U+20BB7, b
    CHECK(d.cps.size() == 4);
    CHECK(d.cps[1] == 0xE9 && d.cps[2] == 0x20BB7);
    CHECK(d.cpByte[2] == 3 && d.cpByte[3] == 7 && d.cpByte[4] == 8);
    CHECK(d.cpUnit[3] == 4 && d.utf16.size() == 5);
    d = forkword::decode("\xE3\x81");  // cut sequence
    CHECK(d.cps.size() == 2 && d.cps[0] == 0xFFFD && d.cpByte[2] == 2);
}

static void testFallback() {
    CHECK_RUNS("foo bar", nullptr, "0-3 3-4s 4-7 ");
    CHECK_RUNS("a.b->c", nullptr, "0-1 1-2 2-3 3-5 5-6 ");
    CHECK_RUNS("caf\xC3\xA9 x", nullptr, "0-5 5-6s 6-7 ");
    std::string s = std::string(kNihongo) + kNo + kTekisuto + kDesu + kMaru;
    CHECK_RUNS(s, nullptr, "0-9 9-12 12-24 24-30 30-33 ");
    CHECK_RUNS(std::string("ab") + kNihongo, nullptr, "0-2 2-11 ");
    CHECK_RUNS(std::string("a") + kWideSpace + "b", nullptr, "0-1 1-4s 4-5 ");
    CHECK_RUNS(kSugoi, nullptr, "0-12 ");
    CHECK_RUNS(std::string("\xF0\xA0\xAE\xB7") + kNihongo, nullptr, "0-13 ");
}

static void testDictionary() {
    std::string tocho = kTokyoTocho;
    std::vector<bool> d = dictAt(tocho, { 2 });
    CHECK_RUNS(tocho, &d, "0-6 6-12 ");
    // The dictionary decides where ASCII meets other text.
    std::string mixed = std::string("ab") + kNihongo;
    d = dictAt(mixed, {});
    CHECK_RUNS(mixed, &d, "0-11 ");
    d = dictAt(mixed, { 2 });
    CHECK_RUNS(mixed, &d, "0-2 2-11 ");
    // Punctuation and spaces stay in one run even where the dictionary splits them.
    std::string punct = std::string(kMaru) + kKagi;
    d = dictAt(punct, { 1 });
    CHECK_RUNS(punct, &d, "0-6 ");
    std::string spaces = std::string(kWideSpace) + kWideSpace;
    d = dictAt(spaces, { 1 });
    CHECK_RUNS(spaces, &d, "0-6s ");
    // ASCII keeps upstream's rules whatever the dictionary says.
    d = dictAt("foo_bar", { 3, 4 });
    CHECK_RUNS("foo_bar", &d, "0-7 ");
}

static void testStops() {
    std::vector<forkword::Run> r = forkword::runs(forkword::decode("foo  bar.baz"), nullptr);
    CHECK(forkword::nextStop(r, 0) == 5);
    CHECK(forkword::nextStop(r, 1) == 5);
    CHECK(forkword::nextStop(r, 3) == 5);
    CHECK(forkword::nextStop(r, 5) == 8);
    CHECK(forkword::nextStop(r, 8) == 9);
    CHECK(forkword::prevStop(r, 12) == 9);
    CHECK(forkword::prevStop(r, 9) == 8);
    CHECK(forkword::prevStop(r, 5) == 0);
    CHECK(forkword::prevStop(r, 4) == 0);
    CHECK(forkword::prevStop(r, 2) == 0);
    r = forkword::runs(forkword::decode("  foo"), nullptr);
    CHECK(forkword::prevStop(r, 2) == 0);
    CHECK(forkword::nextStop(r, 0) == 2);
}

int main() {
    testDecode();
    testFallback();
    testDictionary();
    testStops();
    if (failures > 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all word_stops tests passed\n");
    return 0;
}
