// Unit tests for fork/ListOps.h. Built and run by the "test" job in build-windows.yml:
//   cl /std:c++17 /EHsc /W4 /WX fork\tests\list_ops_test.cpp
#include "../ListOps.h"

#include <cstdio>
#include <string>

using forklist::Lines;
using forklist::Pos;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static std::string show(const Lines& lines) {
    std::string out;
    for (const std::string& l : lines) out += "[" + l + "]";
    return out;
}

static void checkLines(const Lines& actual, const Lines& expected, int line) {
    if (actual == expected) return;
    std::printf("FAIL %s:%d: lines\n  actual:   %s\n  expected: %s\n", __FILE__, line, show(actual).c_str(), show(expected).c_str());
    ++failures;
}
#define CHECK_LINES(actual, ...) checkLines((actual), Lines __VA_ARGS__, __LINE__)

static void checkPos(Pos actual, size_t line, size_t col, int at) {
    if (actual.line == line && actual.col == col) return;
    std::printf("FAIL %s:%d: pos (%zu,%zu), expected (%zu,%zu)\n", __FILE__, at, actual.line, actual.col, line, col);
    ++failures;
}
#define CHECK_POS(actual, line, col) checkPos((actual), (line), (col), __LINE__)

static void testParse() {
    forklist::Item it = forklist::parseItem("\t- [x] done");
    CHECK(it.valid && !it.ordered && it.mark == '-' && it.indent == 4 && it.hasCheckbox && it.contentStart == 7);
    it = forklist::parseItem("  12) text");
    CHECK(it.valid && it.ordered && it.number == 12 && it.mark == ')' && it.indent == 2 && it.contentStart == 6);
    CHECK(forklist::parseItem("-").valid);
    CHECK(!forklist::parseItem("-x").valid);
    CHECK(!forklist::parseItem("1.5 apples").valid);
    CHECK(!forklist::parseItem("**bold**").valid);
    CHECK(!forklist::parseItem("plain").valid);
}

static void testStructure() {
    Lines l = { "- a", "  more a", "\t- a1", "", "- b", "text" };
    CHECK(forklist::ownerItem(l, 1) == 0);
    CHECK(forklist::subtreeEnd(l, 0) == 3);
    CHECK(forklist::parentItem(l, 2) == 0);
    CHECK(forklist::nextSibling(l, 0) == 4);
    CHECK(forklist::prevSibling(l, 4) == 0);
    CHECK(forklist::ownerItem(l, 5) == forklist::npos);
    CHECK(forklist::detectIndentUnit({ "- a", "  - b" }) == "  ");
    CHECK(forklist::detectIndentUnit({ "- a", "\t- b" }) == "\t");
    CHECK(forklist::detectIndentUnit({ "- a" }) == "\t");
}

static void testEnter() {
    const auto C = forklist::BlankItemEnter::Continue;
    const auto O = forklist::BlankItemEnter::Outdent;
    const auto R = forklist::BlankItemEnter::Remove;
    auto r = forklist::onEnter({ "- a" }, 0, 3, "\t", C);
    CHECK(r.handled);
    CHECK_LINES(r.lines, { "- a", "- " });
    CHECK_POS(r.head, 1, 2);

    r = forklist::onEnter({ "- abc" }, 0, 3, "\t", C);
    CHECK_LINES(r.lines, { "- a", "- bc" });
    CHECK_POS(r.head, 1, 2);

    r = forklist::onEnter({ "1. a", "2. b" }, 0, 4, "\t", C);
    CHECK_LINES(r.lines, { "1. a", "2. ", "3. b" });
    CHECK_POS(r.head, 1, 3);

    r = forklist::onEnter({ "1. a", "1. b" }, 1, 4, "\t", C);
    CHECK_LINES(r.lines, { "1. a", "1. b", "1. " });

    r = forklist::onEnter({ "- [x] done" }, 0, 10, "\t", C);
    CHECK_LINES(r.lines, { "- [x] done", "- [ ] " });
    CHECK_POS(r.head, 1, 6);

    // Empty item, Continue: another empty item at the same level.
    r = forklist::onEnter({ "- a", "- " }, 1, 2, "\t", C);
    CHECK_LINES(r.lines, { "- a", "- ", "- " });
    CHECK_POS(r.head, 2, 2);

    r = forklist::onEnter({ "1. a", "2. " }, 1, 3, "\t", C);
    CHECK_LINES(r.lines, { "1. a", "2. ", "3. " });
    CHECK_POS(r.head, 2, 3);

    r = forklist::onEnter({ "- a", "\t- " }, 1, 3, "\t", C);
    CHECK_LINES(r.lines, { "- a", "\t- ", "\t- " });
    CHECK_POS(r.head, 2, 3);

    // Empty item, Outdent: one level out, or the marker is cleared at the top level.
    r = forklist::onEnter({ "- a", "- " }, 1, 2, "\t", O);
    CHECK_LINES(r.lines, { "- a", "" });
    CHECK_POS(r.head, 1, 0);

    r = forklist::onEnter({ "- a", "\t- " }, 1, 3, "\t", O);
    CHECK_LINES(r.lines, { "- a", "- " });
    CHECK_POS(r.head, 1, 2);

    // Empty item, Remove: the marker is cleared at any level.
    r = forklist::onEnter({ "- a", "\t- " }, 1, 3, "\t", R);
    CHECK_LINES(r.lines, { "- a", "" });
    CHECK_POS(r.head, 1, 0);

    CHECK(!forklist::onEnter({ "- a" }, 0, 0, "\t", C).handled);
    CHECK(!forklist::onEnter({ "plain" }, 0, 5, "\t", C).handled);
}

static void testShift() {
    Pos p{ 1, 3 };
    auto r = forklist::shiftItems({ "- a", "- b", "\t- c", "- d" }, 1, 1, false, "\t", p, p);
    CHECK_LINES(r.lines, { "- a", "\t- b", "\t\t- c", "- d" });
    CHECK_POS(r.head, 1, 4);

    p = Pos{ 1, 4 };
    r = forklist::shiftItems({ "- a", "\t- b", "\t\t- c", "- d" }, 1, 1, true, "\t", p, p);
    CHECK_LINES(r.lines, { "- a", "- b", "\t- c", "- d" });
    CHECK_POS(r.head, 1, 3);

    p = Pos{ 1, 4 };
    r = forklist::shiftItems({ "1. a", "2. b", "3. c" }, 1, 1, false, "\t", p, p);
    CHECK_LINES(r.lines, { "1. a", "\t1. b", "2. c" });
    CHECK_POS(r.head, 1, 5);

    p = Pos{ 1, 5 };
    r = forklist::shiftItems({ "1. a", "\t1. b", "2. c" }, 1, 1, true, "\t", p, p);
    CHECK_LINES(r.lines, { "1. a", "2. b", "3. c" });
    CHECK_POS(r.head, 1, 4);

    p = Pos{ 1, 5 };
    r = forklist::shiftItems({ "1. a", "\t1. b" }, 1, 1, true, "\t", p, p);
    CHECK_LINES(r.lines, { "1. a", "2. b" });

    p = Pos{ 1, 4 };
    r = forklist::shiftItems({ "1. a", "1. b", "1. c" }, 1, 1, false, "\t", p, p);
    CHECK_LINES(r.lines, { "1. a", "\t1. b", "1. c" });

    p = Pos{ 0, 2 };
    r = forklist::shiftItems({ "- a", "  - b" }, 0, 1, false, "  ", p, p);
    CHECK_LINES(r.lines, { "  - a", "    - b" });

    p = Pos{ 0, 0 };
    CHECK(!forklist::shiftItems({ "text", "- a" }, 0, 1, false, "\t", p, p).handled);
}

static void testMove() {
    Pos p{ 0, 2 };
    auto r = forklist::moveItem({ "- a", "\t- a1", "- b", "- c" }, 0, false, p, p);
    CHECK_LINES(r.lines, { "- b", "- a", "\t- a1", "- c" });
    CHECK_POS(r.head, 1, 2);

    p = Pos{ 1, 2 };
    r = forklist::moveItem({ "- a", "- b", "\t- b1" }, 1, true, p, p);
    CHECK_LINES(r.lines, { "- b", "\t- b1", "- a" });
    CHECK_POS(r.head, 0, 2);

    p = Pos{ 1, 3 };
    r = forklist::moveItem({ "1. a", "2. b" }, 1, true, p, p);
    CHECK_LINES(r.lines, { "1. b", "2. a" });
    CHECK_POS(r.head, 0, 3);

    p = Pos{ 1, 3 };
    r = forklist::moveItem({ "1. a", "1. b" }, 1, true, p, p);
    CHECK_LINES(r.lines, { "1. b", "1. a" });

    p = Pos{ 0, 2 };
    r = forklist::moveItem({ "- a", "- b" }, 0, true, p, p);
    CHECK(r.handled);
    CHECK_LINES(r.lines, { "- a", "- b" });

    p = Pos{ 0, 0 };
    CHECK(!forklist::moveItem({ "text" }, 0, true, p, p).handled);
}

static void testSelect() {
    Lines l = { "- a", "\t- b", "\t- c", "- d" };
    auto s = forklist::selectItem(l, 1, 2, false);
    CHECK(s.handled && s.first == 1 && s.end == 2);
    s = forklist::selectItem(l, 1, 2, true);
    CHECK(s.first == 0 && s.end == 3);
    s = forklist::selectItem(l, 0, 3, true);
    CHECK(s.first == 0 && s.end == 4);
    s = forklist::selectItem(l, 0, 4, true);
    CHECK(s.first == 0 && s.end == 4);
    CHECK(!forklist::selectItem({ "text" }, 0, 1, false).handled);
}

int main() {
    testParse();
    testStructure();
    testEnter();
    testShift();
    testMove();
    testSelect();
    if (failures > 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all list_ops tests passed\n");
    return 0;
}
