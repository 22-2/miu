// Fork-only additions to struct Editor, included inside its body from Source.cpp.
// Keeping them here limits Source.cpp's diff against upstream to a few hook lines,
// so upstream updates rarely conflict. Keep this file ASCII: MSVC reads it in the
// ANSI code page because the project does not pass /utf-8.

    bool ctrlKChordPending = false;
    // Set by forkHandleKey to let a key bypass upstream's Ctrl+key switch and reach the code after it.
    bool forkSkipCtrlShortcuts = false;

    void selectAllOccurrences() {
        if (cursors.empty()) return;
        if (!cursors.back().hasSelection()) selectNextOccurrence();
        const Cursor c = cursors.back(); if (!c.hasSelection()) return;
        size_t len = c.end() - c.start(); std::string query = pt.getRange(c.start(), len);
        std::vector<Cursor> found;
        // findText wraps around to the top, so stop once a match goes backwards.
        size_t pos = 0;
        while (pos <= pt.length()) { size_t p = findText(pos, query, true, true, false, false); if (p == std::string::npos || p < pos) break; found.push_back({ p + len, p, getXFromPos(p + len) }); pos = p + len; }
        if (found.empty()) return;
        rollbackPadding(); cursors = found; mergeCursors(); ensureCaretVisible(); InvalidateRect(hwnd, NULL, FALSE);
    }
    size_t lineContentEnd(int lineIdx) { size_t end = (lineIdx + 1 < (int)lineStarts.size()) ? lineStarts[lineIdx + 1] : pt.length(); if (end > lineStarts[lineIdx] && pt.charAt(end - 1) == '\n') end--; if (end > lineStarts[lineIdx] && pt.charAt(end - 1) == '\r') end--; return end; }
    // Sublime's Ctrl+L: select whole lines; pressing again extends the selection by one line.
    void expandSelectionToLines() {
        if (cursors.empty() || lineStarts.empty()) return; rollbackPadding();
        for (auto& c : cursors) { size_t s = lineStarts[getLineIdx(c.start())]; int eLine = getLineIdx(c.end()); size_t e = (eLine + 1 < (int)lineStarts.size()) ? lineStarts[eLine + 1] : pt.length(); c = { e, s, getXFromPos(e) }; }
        mergeCursors(); ensureCaretVisible(); InvalidateRect(hwnd, NULL, FALSE);
    }
    // Sublime's Ctrl+Shift+L: put a caret at the end of every selected line.
    void splitSelectionIntoLines() {
        if (cursors.empty() || lineStarts.empty()) return; rollbackPadding();
        std::vector<Cursor> result;
        for (const auto& c : cursors) {
            if (!c.hasSelection()) { result.push_back(c); continue; }
            int sLine = getLineIdx(c.start()); int eLine = getLineIdx(c.end());
            if (eLine > sLine && c.end() == lineStarts[eLine]) eLine--;
            for (int i = sLine; i <= eLine; ++i) { size_t p = std::min(lineContentEnd(i), c.end()); result.push_back({ p, p, getXFromPos(p) }); }
        }
        cursors = result; mergeCursors(); ensureCaretVisible(); InvalidateRect(hwnd, NULL, FALSE);
    }
    void copyFilePath(bool relative) {
        if (currentFilePath.empty()) return;
        std::wstring path = currentFilePath;
        if (relative) {
            // miu has no project root, so paths are relative to the current directory.
            // PathRelativePathToW fails across drives; the absolute path is copied then.
            wchar_t cwd[MAX_PATH]; wchar_t rel[MAX_PATH];
            if (GetCurrentDirectoryW(MAX_PATH, cwd) && PathRelativePathToW(rel, cwd, FILE_ATTRIBUTE_DIRECTORY, currentFilePath.c_str(), FILE_ATTRIBUTE_NORMAL)) { path = rel; if (path.rfind(L".\\", 0) == 0) path.erase(0, 2); }
        }
        setClipboard(WToUTF8(path), false, false);
    }

    // Sublime-style keymap. Called first from WM_KEYDOWN / WM_SYSKEYDOWN.
    // Upstream shortcuts replaced by the keymap are swallowed here so each action keeps a single key.
    // Returns true when the key was consumed.
    bool forkHandleKey(UINT msg, WPARAM vk) {
        forkSkipCtrlShortcuts = false;
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        bool up = vk == VK_UP;
        // Ctrl+K starts a two-stroke chord (Ctrl+K, Ctrl+U/L). Modifier presses between the strokes keep it alive.
        if (ctrlKChordPending && vk != VK_CONTROL && vk != VK_SHIFT && vk != VK_MENU) {
            ctrlKChordPending = false;
            if (ctrl && (vk == 'U' || vk == 'L')) { convertCase(vk == 'U'); return true; }
        }
        if (msg == WM_SYSKEYDOWN) {
            if (vk == VK_UP || vk == VK_DOWN) { if (ctrl || shift) addCursor(up); return true; }
            if (vk == VK_F3) { selectAllOccurrences(); return true; }
            return false;
        }
        if (!ctrl) return false;
        switch (vk) {
        case 'C': if (!shift) return false; copyFilePath(alt); return true;
        case 'D': if (shift) duplicateLines(false); else selectNextOccurrence(); return true;
        case 'L': if (shift) splitSelectionIntoLines(); else expandSelectionToLines(); return true;
        case 'K': if (shift) return false; ctrlKChordPending = true; return true;
        case 'M': jumpToMatchingBracket(); return true;
        case 'B': case 'U': return true;
        case VK_UP: case VK_DOWN:
            // Ctrl+Alt+Shift+Up/Down goes on to upstream's rectangular selection.
            if (alt) { if (shift) { forkSkipCtrlShortcuts = true; return false; } addCursor(up); return true; }
            if (shift) moveLines(up);
            else { scrollVisualLines(up ? -1 : 1); InvalidateRect(hwnd, NULL, FALSE); }
            return true;
        default: return false;
        }
    }
    // Called from the message loop before upstream handles F1/F3/F11 there. Returns true when consumed.
    bool forkPreTranslateKey(WPARAM vk) {
        if (vk != VK_F11) return false;
        if (GetKeyState(VK_SHIFT) & 0x8000) toggleFullScreen();
        return true;
    }
