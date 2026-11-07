#pragma once
// Word stops for Ctrl+Left/Right, Ctrl+Backspace/Delete and double-click.
// Upstream treats every non-ASCII byte as a word character, so a Japanese sentence is one
// "word" up to the next space. Here a line is split into runs: ASCII text keeps upstream's
// rules (word characters, punctuation, spaces), and text with non-ASCII characters is split
// where a dictionary word breaker (Windows' ICU, passed in by the editor) says, or by
// character type (kanji, hiragana, katakana, ...) when that is not available.
// Free of Win32 so it can be unit tested (fork/tests/word_stops_test.cpp). Keep this file ASCII.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace forkword {

enum class Cls { Space, Word, Punct, Kanji, Hiragana, Katakana, Symbol };

// A run of text between two stops, as UTF-8 byte offsets into the analyzed string.
struct Run { size_t start = 0; size_t end = 0; bool space = false; };

struct Decoded {
    std::wstring utf16;                // for the dictionary word breaker
    std::vector<uint32_t> cps;         // code points
    std::vector<size_t> cpByte;        // UTF-8 byte offset of each code point, plus the total size
    std::vector<size_t> cpUnit;        // UTF-16 offset of each code point, plus the total length
};

// Invalid UTF-8 bytes decode one by one as U+FFFD, so offsets always stay on the input bytes.
inline Decoded decode(const std::string& s) {
    Decoded d;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        size_t n = 0;
        uint32_t cp = b;
        if (b < 0x80) n = 1;
        else if ((b >> 5) == 0x6) { n = 2; cp &= 0x1Fu; }
        else if ((b >> 4) == 0xE) { n = 3; cp &= 0x0Fu; }
        else if ((b >> 3) == 0x1E) { n = 4; cp &= 0x07u; }
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; ++k) {
            unsigned char c = static_cast<unsigned char>(s[i + k]);
            if ((c & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (c & 0x3Fu);
        }
        if (!ok) { cp = 0xFFFD; n = 1; }
        d.cps.push_back(cp);
        d.cpByte.push_back(i);
        d.cpUnit.push_back(d.utf16.size());
        if (cp >= 0x10000) {
            d.utf16 += static_cast<wchar_t>(0xD800 + ((cp - 0x10000) >> 10));
            d.utf16 += static_cast<wchar_t>(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            d.utf16 += static_cast<wchar_t>(cp);
        }
        i += n;
    }
    d.cpByte.push_back(s.size());
    d.cpUnit.push_back(d.utf16.size());
    return d;
}

inline bool inRange(uint32_t c, uint32_t lo, uint32_t hi) { return c >= lo && c <= hi; }

inline Cls classOf(uint32_t c) {
    if (c == ' ' || c == '\t' || c == 0x3000) return Cls::Space;
    if (c < 0x80) {
        bool word = inRange(c, 'a', 'z') || inRange(c, 'A', 'Z') || inRange(c, '0', '9') || c == '_';
        return word ? Cls::Word : Cls::Punct;
    }
    if (inRange(c, 0x3041, 0x309F)) return Cls::Hiragana;
    if (c == 0x30FB) return Cls::Symbol;  // katakana middle dot
    if (inRange(c, 0x30A0, 0x30FF) || inRange(c, 0x31F0, 0x31FF) || inRange(c, 0xFF66, 0xFF9F)) return Cls::Katakana;
    if (inRange(c, 0x3400, 0x4DBF) || inRange(c, 0x4E00, 0x9FFF) || inRange(c, 0xF900, 0xFAFF) || inRange(c, 0x20000, 0x3FFFF)
        || inRange(c, 0x3005, 0x3007)) return Cls::Kanji;
    if (inRange(c, 0xFF10, 0xFF19) || inRange(c, 0xFF21, 0xFF3A) || inRange(c, 0xFF41, 0xFF5A)) return Cls::Word;
    if (inRange(c, 0x3000, 0x303F) || inRange(c, 0xFF00, 0xFFEF) || inRange(c, 0x2000, 0x206F)) return Cls::Symbol;
    // Other scripts (accented Latin, Cyrillic, ...) join ASCII words, as upstream does.
    return Cls::Word;
}

inline bool isPunctLike(Cls c) { return c == Cls::Punct || c == Cls::Symbol; }

// dictBreak, when given, has one entry per UTF-16 unit of d.utf16 (plus one): true where
// the dictionary word breaker puts a boundary. Without it, character types decide.
inline std::vector<Run> runs(const Decoded& d, const std::vector<bool>* dictBreak) {
    std::vector<Run> out;
    size_t n = d.cps.size();
    if (n == 0) return out;
    std::vector<Cls> cls(n);
    for (size_t i = 0; i < n; ++i) {
        cls[i] = classOf(d.cps[i]);
        // The long vowel mark belongs to the kana before it ("su-goi" stays one word).
        bool kanaBefore = i > 0 && (cls[i - 1] == Cls::Hiragana || cls[i - 1] == Cls::Katakana);
        if (d.cps[i] == 0x30FC && kanaBefore) cls[i] = cls[i - 1];
    }
    size_t start = 0;
    for (size_t i = 1; i <= n; ++i) {
        bool stop = i == n;
        if (!stop) {
            Cls a = cls[i - 1], b = cls[i];
            bool asciiPair = d.cps[i - 1] < 0x80 && d.cps[i] < 0x80;
            bool spaceChange = (a == Cls::Space) != (b == Cls::Space);
            if (asciiPair || !dictBreak) stop = a != b;
            // Runs of spaces and of punctuation stay whole, as in upstream.
            else stop = spaceChange || (a != Cls::Space && (*dictBreak)[d.cpUnit[i]] && !(isPunctLike(a) && isPunctLike(b)));
        }
        if (!stop) continue;
        out.push_back(Run{ d.cpByte[start], d.cpByte[i], cls[start] == Cls::Space });
        start = i;
    }
    return out;
}

// The run that contains byte offset p. p must be inside the analyzed text.
inline size_t runAt(const std::vector<Run>& r, size_t p) {
    size_t i = 0;
    while (i + 1 < r.size() && r[i].end <= p) ++i;
    return i;
}

// Ctrl+Right from p (p < text size): past the word, then past the spaces after it.
inline size_t nextStop(const std::vector<Run>& r, size_t p) {
    size_t i = runAt(r, p);
    size_t q = r[i].end;
    if (!r[i].space) {
        while (i + 1 < r.size() && r[i + 1].space) q = r[++i].end;
    }
    return q;
}

// Ctrl+Left from p (p > 0): back over the spaces, then to the start of the word before them.
inline size_t prevStop(const std::vector<Run>& r, size_t p) {
    size_t i = runAt(r, p - 1);
    if (r[i].space) {
        if (i == 0) return r[0].start;
        --i;
    }
    return r[i].start;
}

} // namespace forkword
