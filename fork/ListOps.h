#pragma once
// Markdown list editing for the fork: continue items on Enter, indent/outdent and move
// items together with their children, and select items. Free of Win32 so it can be
// unit tested (fork/tests/list_ops_test.cpp).
// Operations take a window of lines (without line endings) around the caret and return
// the edited window plus caret positions; the editor then replaces only changed lines.
// Keep this file ASCII (see EditorFork.inl).
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace forklist {

using Lines = std::vector<std::string>;
constexpr size_t npos = static_cast<size_t>(-1);
constexpr int kTabWidth = 4;

// A list item line such as "  - [ ] text" or "3. text".
struct Item {
    bool valid = false;
    int indent = 0;            // visual width of the leading whitespace (tabs stop every 4 columns)
    size_t indentBytes = 0;
    bool ordered = false;
    long number = 0;
    char mark = 0;             // '-', '*' or '+' for bullets; '.' or ')' after the number
    size_t markerEnd = 0;      // byte just past the bullet or "N."
    bool hasCheckbox = false;
    size_t contentStart = 0;   // byte where the item text starts
};

struct Pos { size_t line = 0; size_t col = 0; };

struct Result {
    bool handled = false;      // false: the key should get its normal behavior
    Lines lines;
    Pos anchor;
    Pos head;
};

struct Range { bool handled = false; size_t first = 0; size_t end = 0; };

inline bool isSpace(char c) { return c == ' ' || c == '\t'; }

inline bool isBlank(const std::string& s) {
    return std::all_of(s.begin(), s.end(), isSpace);
}

inline size_t leadingBytes(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && isSpace(s[i])) ++i;
    return i;
}

inline int indentWidth(const std::string& s) {
    int w = 0;
    for (char c : s) {
        if (c == ' ') ++w;
        else if (c == '\t') w += kTabWidth - w % kTabWidth;
        else break;
    }
    return w;
}

inline Item parseItem(const std::string& s) {
    Item it;
    size_t lead = leadingBytes(s);
    size_t p = lead;
    if (p < s.size() && (s[p] == '-' || s[p] == '*' || s[p] == '+')) {
        it.mark = s[p];
        ++p;
    } else {
        size_t d = p;
        while (d < s.size() && d - p < 9 && s[d] >= '0' && s[d] <= '9') ++d;
        if (d == p || d >= s.size() || (s[d] != '.' && s[d] != ')')) return Item{};
        it.ordered = true;
        it.number = std::stol(s.substr(p, d - p));
        it.mark = s[d];
        p = d + 1;
    }
    // The marker must be followed by whitespace or end the line ("-x" and "1.5" are text).
    if (p < s.size() && !isSpace(s[p])) return Item{};
    it.valid = true;
    it.indent = indentWidth(s);
    it.indentBytes = lead;
    it.markerEnd = p;
    size_t c = p;
    while (c < s.size() && isSpace(s[c])) ++c;
    bool box = s.compare(c, 3, "[ ]") == 0 || s.compare(c, 3, "[x]") == 0 || s.compare(c, 3, "[X]") == 0;
    if (box && (c + 3 == s.size() || isSpace(s[c + 3]))) {
        it.hasCheckbox = true;
        c += 3;
        while (c < s.size() && isSpace(s[c])) ++c;
    }
    it.contentStart = c;
    return it;
}

inline bool isItem(const Lines& L, size_t i) { return i < L.size() && parseItem(L[i]).valid; }

// The item that a line belongs to: the line itself if it is an item, or the item above
// whose text an indented continuation line continues. npos outside of lists.
inline size_t ownerItem(const Lines& L, size_t i) {
    if (i >= L.size()) return npos;
    if (isItem(L, i)) return i;
    if (isBlank(L[i])) return npos;
    int w = indentWidth(L[i]);
    if (w == 0) return npos;
    for (size_t j = i; j-- > 0;) {
        if (isBlank(L[j])) continue;
        Item it = parseItem(L[j]);
        if (it.valid) {
            if (it.indent < w) return j;
            continue;
        }
        if (indentWidth(L[j]) == 0) return npos;
    }
    return npos;
}

// One past the last line of the item and its children. Trailing blank lines are excluded.
inline size_t subtreeEnd(const Lines& L, size_t i) {
    int d = parseItem(L[i]).indent;
    size_t last = i;
    for (size_t j = i + 1; j < L.size(); ++j) {
        if (isBlank(L[j])) continue;
        if (indentWidth(L[j]) <= d) break;
        last = j;
    }
    return last + 1;
}

inline size_t parentItem(const Lines& L, size_t i) {
    int d = parseItem(L[i]).indent;
    for (size_t j = i; j-- > 0;) {
        if (isBlank(L[j])) continue;
        Item it = parseItem(L[j]);
        if (it.valid) {
            if (it.indent < d) return j;
            continue;
        }
        if (indentWidth(L[j]) == 0) return npos;
    }
    return npos;
}

inline size_t prevSibling(const Lines& L, size_t i) {
    int d = parseItem(L[i]).indent;
    for (size_t j = i; j-- > 0;) {
        if (isBlank(L[j])) continue;
        Item it = parseItem(L[j]);
        if (it.valid) {
            if (it.indent == d) return j;
            if (it.indent < d) return npos;
            continue;
        }
        if (indentWidth(L[j]) <= d) return npos;
    }
    return npos;
}

inline size_t nextSibling(const Lines& L, size_t i) {
    int d = parseItem(L[i]).indent;
    size_t j = subtreeEnd(L, i);
    while (j < L.size() && isBlank(L[j])) ++j;
    if (j < L.size()) {
        Item it = parseItem(L[j]);
        if (it.valid && it.indent == d) return j;
    }
    return npos;
}

inline std::vector<size_t> siblings(const Lines& L, size_t i) {
    size_t first = i;
    for (size_t p = prevSibling(L, first); p != npos; p = prevSibling(L, first)) first = p;
    std::vector<size_t> out;
    for (size_t s = first; s != npos; s = nextSibling(L, s)) out.push_back(s);
    return out;
}

inline std::vector<size_t> orderedSiblings(const Lines& L, size_t i) {
    std::vector<size_t> out;
    for (size_t s : siblings(L, i)) {
        if (parseItem(L[s]).ordered) out.push_back(s);
    }
    return out;
}

// "1. 1. 1." style lists, where every item keeps the same number on purpose.
inline bool isLazyNumbering(const Lines& L, size_t i) {
    std::vector<size_t> ordered = orderedSiblings(L, i);
    if (ordered.size() < 2) return false;
    long first = parseItem(L[ordered[0]]).number;
    return std::all_of(ordered.begin(), ordered.end(), [&](size_t s) { return parseItem(L[s]).number == first; });
}

inline void setNumber(std::string& s, long n) {
    Item it = parseItem(s);
    if (!it.valid || !it.ordered) return;
    s.replace(it.indentBytes, it.markerEnd - 1 - it.indentBytes, std::to_string(n));
}

// Callers check isLazyNumbering on the list before editing it, since an edit can make
// a numbered list look lazy (for example "1. b, 1. a" right after moving an item up).
// Numbers the ordered items among the siblings of item i from the first item's number.
inline void renumberGroup(Lines& L, size_t i) {
    if (!isItem(L, i)) return;
    std::vector<size_t> ordered = orderedSiblings(L, i);
    if (ordered.empty()) return;
    long first = parseItem(L[ordered[0]]).number;
    for (size_t k = 1; k < ordered.size(); ++k) setNumber(L[ordered[k]], first + static_cast<long>(k));
}

// Moves a column that was after the marker by the change in the marker's length.
inline size_t shiftCol(const std::string& before, const std::string& after, size_t col) {
    Item a = parseItem(before), b = parseItem(after);
    if (!a.valid || !b.valid || col < a.markerEnd) return col;
    return col + b.markerEnd - a.markerEnd;
}

inline void renumberKeepingCarets(Result& r, const std::vector<size_t>& items) {
    std::string anchorLine = r.anchor.line < r.lines.size() ? r.lines[r.anchor.line] : std::string();
    std::string headLine = r.head.line < r.lines.size() ? r.lines[r.head.line] : std::string();
    for (size_t i : items) renumberGroup(r.lines, i);
    if (r.anchor.line < r.lines.size()) r.anchor.col = shiftCol(anchorLine, r.lines[r.anchor.line], r.anchor.col);
    if (r.head.line < r.lines.size()) r.head.col = shiftCol(headLine, r.lines[r.head.line], r.head.col);
}

// The indent unit the list already uses: a tab, or the smallest space indent of an item.
// Defaults to a tab, which is what miu's own Tab inserts.
inline std::string detectIndentUnit(const Lines& L) {
    size_t minSpaces = 0;
    for (const std::string& s : L) {
        Item it = parseItem(s);
        if (!it.valid || it.indentBytes == 0) continue;
        if (s.find('\t', 0) < it.indentBytes) return "\t";
        if (minSpaces == 0 || it.indentBytes < minSpaces) minSpaces = it.indentBytes;
    }
    return minSpaces == 0 ? std::string("\t") : std::string(minSpaces, ' ');
}

inline size_t clampSub(size_t col, size_t n) { return col > n ? col - n : 0; }

// Tab / Shift+Tab: indents or outdents every item touched by lines [first, last] together
// with its children. Not handled when a non-blank selected line is outside of a list.
inline Result shiftItems(const Lines& in, size_t first, size_t last, bool outdent, const std::string& unit, Pos anchor, Pos head) {
    Result r;
    std::vector<size_t> roots;
    for (size_t i = first; i <= last && i < in.size(); ++i) {
        if (isBlank(in[i])) continue;
        size_t o = ownerItem(in, i);
        if (o == npos) return r;
        // Drop roots that turn out to be inside this item's subtree, and skip this item
        // when it is inside an earlier root's subtree.
        while (!roots.empty() && roots.back() > o && roots.back() < subtreeEnd(in, o)) roots.pop_back();
        if (!roots.empty() && o >= roots.back() && o < subtreeEnd(in, roots.back())) continue;
        roots.push_back(o);
    }
    if (roots.empty()) return r;
    r.handled = true;
    r.lines = in;
    r.anchor = anchor;
    r.head = head;
    size_t unitSpaces = unit == "\t" ? static_cast<size_t>(kTabWidth) : unit.size();
    std::vector<size_t> renumber;
    for (size_t root : roots) {
        if (outdent && parseItem(in[root]).indent == 0) continue;
        bool lazy = isLazyNumbering(in, root);
        size_t prev = prevSibling(in, root), next = nextSibling(in, root);
        size_t end = subtreeEnd(in, root);
        for (size_t j = root; j < end; ++j) {
            if (isBlank(in[j])) continue;
            std::string& s = r.lines[j];
            if (outdent) {
                size_t n = s[0] == '\t' ? 1 : std::min(leadingBytes(s), unitSpaces);
                s.erase(0, n);
                if (r.anchor.line == j) r.anchor.col = clampSub(r.anchor.col, n);
                if (r.head.line == j) r.head.col = clampSub(r.head.col, n);
            } else {
                s.insert(0, unit);
                if (r.anchor.line == j && r.anchor.col > 0) r.anchor.col += unit.size();
                if (r.head.line == j && r.head.col > 0) r.head.col += unit.size();
            }
        }
        // An item indented into a new child list starts that list at 1.
        if (lazy) continue;
        if (!outdent && prevSibling(r.lines, root) == npos) {
            std::string before = r.lines[root];
            setNumber(r.lines[root], 1);
            if (r.anchor.line == root) r.anchor.col = shiftCol(before, r.lines[root], r.anchor.col);
            if (r.head.line == root) r.head.col = shiftCol(before, r.lines[root], r.head.col);
        }
        renumber.push_back(root);
        if (prev != npos) renumber.push_back(prev);
        if (next != npos) renumber.push_back(next);
    }
    renumberKeepingCarets(r, renumber);
    return r;
}

// What Enter does on an empty item (the fork.ini setting list.blankItemEnter).
enum class BlankItemEnter {
    Continue,  // add another empty item at the same level
    Outdent,   // move the item one level out; at the top level, clear the marker
    Remove,    // clear the marker, leaving an empty line
};

// Enter at or after an item's text start: continues the list with a new item, carrying the
// rest of the line. On an empty item it does what `blank` says.
inline Result onEnter(const Lines& in, size_t line, size_t col, const std::string& unit, BlankItemEnter blank) {
    Result r;
    if (line >= in.size()) return r;
    const std::string& s = in[line];
    Item it = parseItem(s);
    if (!it.valid || col < it.contentStart || col > s.size()) return r;
    Pos caret{ line, col };
    if (blank != BlankItemEnter::Continue && isBlank(s.substr(it.contentStart))) {
        if (blank == BlankItemEnter::Outdent && it.indent > 0) return shiftItems(in, line, line, true, unit, caret, caret);
        r.handled = true;
        r.lines = in;
        size_t prev = prevSibling(in, line), next = nextSibling(in, line);
        r.lines[line].clear();
        r.anchor = r.head = Pos{ line, 0 };
        if (isLazyNumbering(in, line)) return r;
        if (prev != npos) renumberKeepingCarets(r, { prev });
        else if (next != npos) renumberKeepingCarets(r, { next });
        return r;
    }
    std::string before = s.substr(0, col);
    while (before.size() > it.contentStart && isSpace(before.back())) before.pop_back();
    std::string after = s.substr(col);
    after.erase(0, leadingBytes(after));
    bool lazy = isLazyNumbering(in, line);
    std::string marker = it.ordered
        ? std::to_string(lazy ? it.number : it.number + 1) + it.mark
        : std::string(1, it.mark);
    std::string prefix = s.substr(0, it.indentBytes) + marker + " " + (it.hasCheckbox ? "[ ] " : "");
    r.handled = true;
    r.lines = in;
    r.lines[line] = before;
    r.lines.insert(r.lines.begin() + static_cast<std::ptrdiff_t>(line) + 1, prefix + after);
    if (!lazy) renumberGroup(r.lines, line + 1);
    r.anchor = r.head = Pos{ line + 1, parseItem(r.lines[line + 1]).contentStart };
    return r;
}

// Moves the item at `line` with its children above the previous sibling or below the next
// one. Not handled outside of lists or when the selection leaves the item.
inline Result moveItem(const Lines& in, size_t line, bool up, Pos anchor, Pos head) {
    Result r;
    size_t a = ownerItem(in, line);
    if (a == npos) return r;
    size_t b = subtreeEnd(in, a);
    if (anchor.line < a || anchor.line >= b) return r;
    r.handled = true;
    r.lines = in;
    r.anchor = anchor;
    r.head = head;
    auto range = [&](size_t from, size_t to) { return Lines(in.begin() + static_cast<std::ptrdiff_t>(from), in.begin() + static_cast<std::ptrdiff_t>(to)); };
    // The list keeps its start number even when its first item moves.
    std::vector<size_t> orderedBefore = orderedSiblings(in, a);
    long startNumber = orderedBefore.empty() ? 0 : parseItem(in[orderedBefore.front()]).number;
    bool lazy = isLazyNumbering(in, a);
    Lines out = range(0, up ? 0 : a);
    size_t movedTo;
    if (up) {
        size_t p = prevSibling(in, a);
        if (p == npos) return r;
        size_t pe = subtreeEnd(in, p);
        out = range(0, p);
        for (const Lines& part : { range(a, b), range(pe, a), range(p, pe), range(b, in.size()) }) out.insert(out.end(), part.begin(), part.end());
        movedTo = p;
    } else {
        size_t n = nextSibling(in, a);
        if (n == npos) return r;
        size_t ne = subtreeEnd(in, n);
        for (const Lines& part : { range(n, ne), range(b, n), range(a, b), range(ne, in.size()) }) out.insert(out.end(), part.begin(), part.end());
        movedTo = a + ne - b;
    }
    r.lines = out;
    r.anchor.line = r.anchor.line - a + movedTo;
    r.head.line = r.head.line - a + movedTo;
    if (lazy) return r;
    std::vector<size_t> orderedAfter = orderedSiblings(r.lines, movedTo);
    if (!orderedAfter.empty()) {
        size_t firstItem = orderedAfter.front();
        std::string before = r.lines[firstItem];
        setNumber(r.lines[firstItem], startNumber);
        if (r.anchor.line == firstItem) r.anchor.col = shiftCol(before, r.lines[firstItem], r.anchor.col);
        if (r.head.line == firstItem) r.head.col = shiftCol(before, r.lines[firstItem], r.head.col);
    }
    renumberKeepingCarets(r, { movedTo });
    return r;
}

// Lines [first, end) are selected. The first call selects the item with its children;
// each further call grows the selection to the parent item, then to the whole list.
// wholeLines is false when nothing or only part of a line is selected.
inline Range selectItem(const Lines& L, size_t first, size_t end, bool wholeLines) {
    size_t o = ownerItem(L, first);
    if (o == npos) return Range{};
    if (!wholeLines) return Range{ true, o, subtreeEnd(L, o) };
    auto grows = [&](size_t s, size_t e) { return s <= first && e >= end && (s < first || e > end); };
    size_t top = o;
    for (size_t c = o; c != npos; c = parentItem(L, c)) {
        size_t e = subtreeEnd(L, c);
        if (grows(c, e)) return Range{ true, c, e };
        top = c;
    }
    std::vector<size_t> sib = siblings(L, top);
    size_t s = sib.front(), e = subtreeEnd(L, sib.back());
    if (grows(s, e)) return Range{ true, s, e };
    return Range{ true, first, end };
}

} // namespace forklist
