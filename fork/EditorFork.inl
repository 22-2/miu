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
    // Ctrl+Backspace / Ctrl+Delete: delete back to the previous / up to the next word stop,
    // the same stops as Ctrl+Left / Ctrl+Right. Carets with a selection delete only the selection.
    void deleteWord(bool forward) {
        if (cursors.empty()) return;
        rollbackPadding(); isRectSelecting = false;
        std::vector<Cursor> original = cursors;
        bool anyRange = false;
        for (auto& c : cursors) {
            if (!c.hasSelection()) { c.anchor = c.head; c.head = forward ? moveWordRight(c.head) : moveWordLeft(c.head); }
            anyRange = anyRange || c.hasSelection();
        }
        if (!anyRange) { cursors = original; return; }
        mergeCursors(); insertAtCursors("");
        // Undo should bring back the carets as they were, not the word ranges built above.
        if (!undo.undoStack.empty()) undo.undoStack.back().beforeCursors = original;
        InvalidateRect(hwnd, NULL, FALSE);
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

    // ---- Markdown list editing. The logic lives in fork/ListOps.h; this part feeds it
    // a window of lines around the caret and writes the result back as one undo step.

    // Lists are edited only in Markdown and plain text, so "- " or "1." in code stays untouched.
    bool forkIsListDocument() const {
        const std::wstring& e = currentExtension;
        return e.empty() || e == L"md" || e == L"markdown" || e == L"mdx" || e == L"txt";
    }
    std::string forkLineText(int i) { return pt.getRange(lineStarts[i], lineContentEnd(i) - lineStarts[i]); }
    // A line at column 0 that is neither blank nor a list item ends the list.
    bool forkIsListBoundary(int i) {
        std::string s = forkLineText(i);
        return !forklist::isBlank(s) && forklist::indentWidth(s) == 0 && !forklist::parseItem(s).valid;
    }
    struct ForkListContext { int first = 0; forklist::Lines lines; forklist::Pos anchor; forklist::Pos head; };
    // Collects the lines that can belong to the list around the caret (at most kMaxSpan
    // lines each way, to stay fast in huge files). Single caret only.
    bool forkListContext(ForkListContext& ctx) {
        const int kMaxSpan = 2000;
        if (!forkIsListDocument() || cursors.size() != 1 || isRectSelecting || lineStarts.empty()) return false;
        const Cursor& c = cursors[0];
        int headLine = getLineIdx(c.head), anchorLine = getLineIdx(c.anchor);
        int first = std::min(headLine, anchorLine), last = std::max(headLine, anchorLine);
        while (first > 0 && headLine - first < kMaxSpan && !forkIsListBoundary(first - 1)) --first;
        while (last + 1 < (int)lineStarts.size() && last - headLine < kMaxSpan && !forkIsListBoundary(last + 1)) ++last;
        ctx.first = first;
        for (int i = first; i <= last; ++i) ctx.lines.push_back(forkLineText(i));
        ctx.head = { (size_t)(headLine - first), c.head - lineStarts[headLine] };
        ctx.anchor = { (size_t)(anchorLine - first), c.anchor - lineStarts[anchorLine] };
        return true;
    }
    size_t forkListPos(int first, forklist::Pos p) {
        int line = std::min(first + (int)p.line, (int)lineStarts.size() - 1);
        return std::min(lineStarts[line] + p.col, lineContentEnd(line));
    }
    void forkSetListCursor(int first, forklist::Pos anchor, forklist::Pos head) {
        size_t a = forkListPos(first, anchor), h = forkListPos(first, head);
        cursors.clear(); cursors.push_back({ h, a, getXFromPos(h) });
    }
    // Replaces only the lines that differ between ctx.lines and r.lines.
    void forkApplyListResult(const ForkListContext& ctx, const forklist::Result& r) {
        const forklist::Lines& oldL = ctx.lines; const forklist::Lines& newL = r.lines;
        if (oldL == newL) { forkSetListCursor(ctx.first, r.anchor, r.head); ensureCaretVisible(); InvalidateRect(hwnd, NULL, FALSE); return; }
        size_t pre = 0;
        while (pre < oldL.size() && pre < newL.size() && oldL[pre] == newL[pre]) ++pre;
        size_t suf = 0;
        while (suf < oldL.size() - pre && suf < newL.size() - pre && oldL[oldL.size() - 1 - suf] == newL[newL.size() - 1 - suf]) ++suf;
        commitPadding();
        int totalLines = (int)lineStarts.size();
        int s = ctx.first + (int)pre, k = ctx.first + (int)(oldL.size() - suf);
        size_t start = lineStarts[s];
        size_t end = k < totalLines ? lineStarts[k] : pt.length();
        // Old lines [s, k) are replaced with their line endings, except that the document's
        // last line has none; text appended after the last line needs a leading newline instead.
        bool appendAtEnd = k >= totalLines && s == k;
        std::string newText;
        for (size_t i = pre; i < newL.size() - suf; ++i) {
            bool lastNew = i + 1 == newL.size() - suf;
            if (appendAtEnd) newText += newlineStr;
            newText += newL[i];
            if (!appendAtEnd && (k < totalLines || !lastNew)) newText += newlineStr;
        }
        EditBatch batch; batch.beforeCursors = cursors;
        std::string oldText = pt.getRange(start, end - start);
        if (!oldText.empty()) { pt.erase(start, oldText.size()); batch.ops.push_back({ EditOp::Erase, start, oldText }); }
        if (!newText.empty()) { pt.insert(start, newText); batch.ops.push_back({ EditOp::Insert, start, newText }); }
        rebuildLineStarts();
        forkSetListCursor(ctx.first, r.anchor, r.head);
        batch.afterCursors = cursors; undo.push(batch); ensureCaretVisible(); updateDirtyFlag(); InvalidateRect(hwnd, NULL, FALSE);
    }
    // Enter (from WM_CHAR): continue the list. Shift+Enter keeps a plain line break.
    bool forkListEnter() {
        if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return false;
        ForkListContext ctx;
        if (!forkListContext(ctx) || cursors[0].hasSelection()) return false;
        forklist::Result r = forklist::onEnter(ctx.lines, ctx.head.line, ctx.head.col, forklist::detectIndentUnit(ctx.lines));
        if (!r.handled) return false;
        forkApplyListResult(ctx, r);
        return true;
    }
    // Tab / Shift+Tab: indent or outdent the items under the caret or selection with their children.
    bool forkListShift(bool outdent) {
        if (!emmetTabstops.empty()) return false;
        ForkListContext ctx;
        if (!forkListContext(ctx)) return false;
        size_t first = std::min(ctx.anchor.line, ctx.head.line), last = std::max(ctx.anchor.line, ctx.head.line);
        // A selection ending at the start of a line does not include that line.
        forklist::Pos endPos = ctx.anchor.line > ctx.head.line ? ctx.anchor : ctx.head;
        if (last > first && endPos.col == 0) --last;
        forklist::Result r = forklist::shiftItems(ctx.lines, first, last, outdent, forklist::detectIndentUnit(ctx.lines), ctx.anchor, ctx.head);
        if (!r.handled) return false;
        forkApplyListResult(ctx, r);
        return true;
    }
    // Ctrl+Shift+Up/Down on a list item: move it with its children past its sibling.
    bool forkListMove(bool up) {
        ForkListContext ctx;
        if (!forkListContext(ctx)) return false;
        forklist::Result r = forklist::moveItem(ctx.lines, ctx.head.line, up, ctx.anchor, ctx.head);
        if (!r.handled) return false;
        forkApplyListResult(ctx, r);
        return true;
    }
    // Ctrl+Shift+Space: select the item with its children; repeat to grow to the parent, then the list.
    bool forkListSelect() {
        ForkListContext ctx;
        if (!forkListContext(ctx)) return false;
        const Cursor& c = cursors[0];
        size_t first = std::min(ctx.anchor.line, ctx.head.line);
        size_t endLine = std::max(ctx.anchor.line, ctx.head.line);
        size_t selEnd = c.end();
        int endDocLine = ctx.first + (int)endLine;
        // Whole lines: starts at a line start and ends at the next line's start or the document end.
        bool wholeLines = c.hasSelection() && c.start() == lineStarts[ctx.first + first]
            && (selEnd == pt.length() || (selEnd == lineStarts[endDocLine] && endLine > first));
        size_t end = (wholeLines && selEnd == lineStarts[endDocLine] && endLine > first) ? endLine : endLine + 1;
        forklist::Range range = forklist::selectItem(ctx.lines, first, end, wholeLines);
        if (!range.handled) return false;
        int startLine = ctx.first + (int)range.first, stopLine = ctx.first + (int)range.end;
        size_t a = lineStarts[startLine];
        size_t h = stopLine < (int)lineStarts.size() ? lineStarts[stopLine] : pt.length();
        rollbackPadding(); cursors.clear(); cursors.push_back({ h, a, getXFromPos(h) });
        ensureCaretVisible(); InvalidateRect(hwnd, NULL, FALSE);
        return true;
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
        if (vk == VK_TAB && !ctrl && !alt) return forkListShift(shift);
        if (!ctrl) return false;
        switch (vk) {
        case VK_SPACE: if (!shift) return false; return forkListSelect();
        case VK_BACK: deleteWord(false); return true;
        case VK_DELETE: deleteWord(true); return true;
        case 'C': if (!shift) return false; copyFilePath(alt); return true;
        case 'D': if (shift) duplicateLines(false); else selectNextOccurrence(); return true;
        case 'L': if (shift) splitSelectionIntoLines(); else expandSelectionToLines(); return true;
        case 'K': if (shift) return false; ctrlKChordPending = true; return true;
        case 'M': jumpToMatchingBracket(); return true;
        case 'B': case 'U': return true;
        case VK_UP: case VK_DOWN:
            // Ctrl+Alt+Shift+Up/Down goes on to upstream's rectangular selection.
            if (alt) { if (shift) { forkSkipCtrlShortcuts = true; return false; } addCursor(up); return true; }
            if (shift) { if (!forkListMove(up)) moveLines(up); }
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
