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

    // ---- Word stops (fork/WordStops.h) for Ctrl+Left/Right, Ctrl+Backspace/Delete and
    // double-click. The dictionary word breaker is Windows' own ICU (icu.dll, Windows 10 1903
    // and later), loaded at run time; without it, character types decide.
    struct ForkIcu {
        typedef void* (__cdecl* OpenFn)(int type, const char* locale, const wchar_t* text, int32_t length, int* status);
        typedef int32_t (__cdecl* StepFn)(void* iter);
        typedef void (__cdecl* CloseFn)(void* iter);
        OpenFn open = nullptr; StepFn first = nullptr; StepFn next = nullptr; CloseFn close = nullptr;
    };
    static const ForkIcu& forkIcu() {
        static const ForkIcu icu = [] {
            ForkIcu r;
            HMODULE m = LoadLibraryExW(L"icu.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!m) return r;
            r.open = reinterpret_cast<ForkIcu::OpenFn>(GetProcAddress(m, "ubrk_open"));
            r.first = reinterpret_cast<ForkIcu::StepFn>(GetProcAddress(m, "ubrk_first"));
            r.next = reinterpret_cast<ForkIcu::StepFn>(GetProcAddress(m, "ubrk_next"));
            r.close = reinterpret_cast<ForkIcu::CloseFn>(GetProcAddress(m, "ubrk_close"));
            if (!r.open || !r.first || !r.next || !r.close) r = ForkIcu{};
            return r;
        }();
        return icu;
    }
    // One flag per UTF-16 unit (plus one), true where ICU puts a word boundary. Empty without ICU.
    static std::vector<bool> forkDictBreaks(const std::wstring& text) {
        const int kWordBreak = 1, kDone = -1;  // UBRK_WORD, UBRK_DONE
        const ForkIcu& icu = forkIcu();
        if (!icu.open || text.empty()) return {};
        int status = 0;
        void* iter = icu.open(kWordBreak, "ja", text.c_str(), (int32_t)text.size(), &status);
        if (status > 0 || !iter) { if (iter) icu.close(iter); return {}; }  // U_FAILURE
        std::vector<bool> flags(text.size() + 1, false);
        for (int32_t p = icu.first(iter); p != kDone; p = icu.next(iter)) {
            if (p >= 0 && (size_t)p <= text.size()) flags[(size_t)p] = true;
        }
        icu.close(iter);
        return flags;
    }
    // Runs of the line around pos, as document offsets. Only kSpan bytes each way are read
    // so very long lines stay fast; a stop at the edge of that window is still a usable stop.
    std::vector<forkword::Run> forkWordRuns(size_t pos) {
        const size_t kSpan = 4096;
        int line = getLineIdx(pos);
        size_t ls = lineStarts[line], le = lineContentEnd(line);
        size_t ws = pos > ls + kSpan ? pos - kSpan : ls;
        size_t we = le > pos + kSpan ? pos + kSpan : le;
        // Do not cut a UTF-8 sequence.
        while (ws > ls && ((unsigned char)pt.charAt(ws) & 0xC0) == 0x80) ws--;
        while (we < le && ((unsigned char)pt.charAt(we) & 0xC0) == 0x80) we++;
        forkword::Decoded d = forkword::decode(pt.getRange(ws, we - ws));
        std::vector<bool> breaks = forkDictBreaks(d.utf16);
        std::vector<forkword::Run> runs = forkword::runs(d, breaks.empty() ? nullptr : &breaks);
        for (auto& r : runs) { r.start += ws; r.end += ws; }
        return runs;
    }
    bool forkIsLineBreakAt(size_t p) { return p >= pt.length() || pt.charAt(p) == '\r' || pt.charAt(p) == '\n'; }
    // Hooked at the top of upstream's moveWordLeft / moveWordRight. Returns npos to leave
    // line breaks to upstream.
    size_t forkMoveWord(size_t pos, bool forward) {
        if (lineStarts.empty()) return std::string::npos;
        size_t list = forkListCaretStop(pos, forward);
        if (list != std::string::npos) return list;
        if (forward) {
            if (forkIsLineBreakAt(pos)) return std::string::npos;
            std::vector<forkword::Run> runs = forkWordRuns(pos);
            return runs.empty() ? std::string::npos : forkword::nextStop(runs, pos);
        }
        if (pos == 0 || forkIsLineBreakAt(pos - 1)) return std::string::npos;
        std::vector<forkword::Run> runs = forkWordRuns(pos);
        return runs.empty() ? std::string::npos : forkword::prevStop(runs, pos);
    }
    // Hooked at the top of upstream's selectWordAt (double-click). Like upstream, a click on
    // a space or the line end right after a word selects that word. Line breaks stay upstream's.
    bool forkSelectWordAt(size_t pos, bool addSelection) {
        if (lineStarts.empty()) return false;
        size_t p = pos;
        bool onSpaceOrEnd = forkIsLineBreakAt(p);
        if (!onSpaceOrEnd) { std::vector<forkword::Run> here = forkWordRuns(p); onSpaceOrEnd = here[forkword::runAt(here, p)].space; }
        if (onSpaceOrEnd && p > 0 && !forkIsLineBreakAt(p - 1)) {
            std::vector<forkword::Run> before = forkWordRuns(p - 1);
            if (!before[forkword::runAt(before, p - 1)].space) p = p - 1;
        }
        if (forkIsLineBreakAt(p)) return false;
        std::vector<forkword::Run> runs = forkWordRuns(p);
        const forkword::Run& r = runs[forkword::runAt(runs, p)];
        Cursor newCur = { r.end, r.start, getXFromPos(r.end) };
        if (addSelection) {
            std::vector<Cursor> kept;
            for (const auto& existing : cursors) if (existing.hasSelection()) kept.push_back(existing);
            kept.push_back(newCur);
            cursors = std::move(kept);
            mergeCursors();
        } else {
            cursors.clear();
            cursors.push_back(newCur);
        }
        return true;
    }

    // ---- IME reconversion: the Henkan key on selected text (or at the caret). The IME asks
    // for the text around the selection (IMR_RECONVERTSTRING), then says which part it will
    // convert (IMR_CONFIRMRECONVERTSTRING); that part is selected so the result replaces it.
    size_t forkReconvStart = 0;           // document offset of the text given to the IME
    std::vector<size_t> forkReconvBytes;  // byte offset from forkReconvStart of each UTF-16 unit, plus the end
    LRESULT forkReconvertString(RECONVERTSTRING* rs) {
        const size_t kSpan = 1024;
        if (cursors.size() != 1 || isRectSelecting || lineStarts.empty()) return 0;
        size_t s = cursors[0].start(), e = cursors[0].end();
        int line = getLineIdx(s);
        size_t ts = s, te = e;
        // A selection inside one line goes with the rest of its line, which helps the IME
        // choose words; a selection over several lines goes alone.
        if (e <= lineContentEnd(line)) {
            size_t ls = lineStarts[line], le = lineContentEnd(line);
            ts = s > ls + kSpan ? s - kSpan : ls;
            te = le > e + kSpan ? e + kSpan : le;
            while (ts > ls && ((unsigned char)pt.charAt(ts) & 0xC0) == 0x80) ts--;
            while (te < le && ((unsigned char)pt.charAt(te) & 0xC0) == 0x80) te++;
        }
        forkword::Decoded d = forkword::decode(pt.getRange(ts, te - ts));
        DWORD size = (DWORD)(sizeof(RECONVERTSTRING) + (d.utf16.size() + 1) * sizeof(wchar_t));
        if (!rs) return size;
        if (rs->dwSize < size) return 0;
        auto unitOf = [&](size_t byte) {
            size_t k = (size_t)(std::lower_bound(d.cpByte.begin(), d.cpByte.end(), byte) - d.cpByte.begin());
            return d.cpUnit[std::min(k, d.cpUnit.size() - 1)];
        };
        size_t compStart = unitOf(s - ts), compEnd = unitOf(e - ts);
        rs->dwVersion = 0;
        rs->dwStrLen = (DWORD)d.utf16.size();
        rs->dwStrOffset = sizeof(RECONVERTSTRING);
        rs->dwCompStrLen = rs->dwTargetStrLen = (DWORD)(compEnd - compStart);
        rs->dwCompStrOffset = rs->dwTargetStrOffset = (DWORD)(compStart * sizeof(wchar_t));
        wchar_t* str = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(rs) + rs->dwStrOffset);
        std::copy(d.utf16.begin(), d.utf16.end(), str);
        str[d.utf16.size()] = L'\0';
        forkReconvStart = ts;
        forkReconvBytes.assign(d.utf16.size() + 1, 0);
        for (size_t k = 0; k + 1 < d.cpUnit.size(); ++k) {
            for (size_t u = d.cpUnit[k]; u < d.cpUnit[k + 1]; ++u) forkReconvBytes[u] = d.cpByte[k];
        }
        forkReconvBytes[d.utf16.size()] = d.cpByte.back();
        return size;
    }
    LRESULT forkConfirmReconvert(const RECONVERTSTRING* rs) {
        if (!rs || forkReconvBytes.empty()) return FALSE;
        size_t from = rs->dwCompStrOffset / sizeof(wchar_t), to = from + rs->dwCompStrLen;
        if (to >= forkReconvBytes.size()) return FALSE;
        size_t a = forkReconvStart + forkReconvBytes[from], b = forkReconvStart + forkReconvBytes[to];
        if (b > pt.length()) return FALSE;
        rollbackPadding(); isRectSelecting = false;
        cursors.clear(); cursors.push_back({ b, a, getXFromPos(b) });
        InvalidateRect(hwnd, NULL, FALSE);
        return TRUE;
    }
    // Called for WM_IME_REQUEST; other requests keep the default handling.
    LRESULT forkImeRequest(WPARAM wParam, LPARAM lParam) {
        if (wParam == IMR_RECONVERTSTRING) return forkReconvertString(reinterpret_cast<RECONVERTSTRING*>(lParam));
        if (wParam == IMR_CONFIRMRECONVERTSTRING) return forkConfirmReconvert(reinterpret_cast<const RECONVERTSTRING*>(lParam));
        return DefWindowProc(hwnd, WM_IME_REQUEST, wParam, lParam);
    }

    // ---- Fork settings: %APPDATA%\miu\fork.ini, opened with Ctrl+, and read again whenever
    // the file's timestamp changes, so saving it in miu applies the change right away.
    struct ForkSettings {
        forklist::BlankItemEnter blankItemEnter = forklist::BlankItemEnter::Continue;
        bool saveOnFocusLost = true;
    };
    ForkSettings forkSettingsCache;
    FILETIME forkSettingsStamp = {};
    bool forkSettingsFromFile = false;
    static std::wstring forkSettingsPath() {
        wchar_t appData[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return L"";
        return std::wstring(appData) + L"\\miu\\fork.ini";
    }
    // Unknown or misspelled values fall back to the defaults; the template lists the valid ones.
    const ForkSettings& forkSettings() {
        std::wstring path = forkSettingsPath();
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
            forkSettingsCache = ForkSettings{}; forkSettingsFromFile = false;
            return forkSettingsCache;
        }
        if (forkSettingsFromFile && CompareFileTime(&info.ftLastWriteTime, &forkSettingsStamp) == 0) return forkSettingsCache;
        ForkSettings s;
        wchar_t value[64];
        GetPrivateProfileStringW(L"list", L"blankItemEnter", L"continue", value, 64, path.c_str());
        if (_wcsicmp(value, L"outdent") == 0) s.blankItemEnter = forklist::BlankItemEnter::Outdent;
        else if (_wcsicmp(value, L"remove") == 0) s.blankItemEnter = forklist::BlankItemEnter::Remove;
        GetPrivateProfileStringW(L"file", L"saveOnFocusLost", L"true", value, 64, path.c_str());
        s.saveOnFocusLost = _wcsicmp(value, L"false") != 0;
        forkSettingsCache = s; forkSettingsStamp = info.ftLastWriteTime; forkSettingsFromFile = true;
        return forkSettingsCache;
    }
    // ---- Save on focus lost (WM_ACTIVATEAPP), like Sublime's save_on_focus_lost.
    // Skipped when saving could surprise: untitled files (would need Save As), files changed
    // on disk since they were loaded (would overwrite those changes), and files miu cannot
    // write without elevation (would raise a UAC prompt while you work in another app).
    bool forkCanAutoSave() {
        if (!forkSettings().saveOnFocusLost || !isDirty || currentFilePath.empty() || isCheckingModification) return false;
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(currentFilePath.c_str(), GetFileExInfoStandard, &fad)) {
            if (CompareFileTime(&lastWriteTime, &fad.ftLastWriteTime) < 0) return false;
            if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) return false;
            HANDLE h = CreateFileW(currentFilePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) return false;
            CloseHandle(h);
        }
        return true;
    }
    void forkOnAppDeactivated() {
        if (!forkCanAutoSave()) return;
        commitPadding();
        // saveFile reopens the file, which clears the undo history. Keep it when the reopened
        // text has the same size and sample hash as what was saved; otherwise (for example a
        // lossy encoding) the recorded positions may not match, so the history stays cleared.
        auto undoStack = undo.undoStack; auto redoStack = undo.redoStack;
        size_t len = pt.length(); uint32_t hash = calcSampleHash(0, len);
        if (!saveFile(currentFilePath)) return;
        if (pt.length() != len || calcSampleHash(0, len) != hash) return;
        undo.undoStack = std::move(undoStack); undo.redoStack = std::move(redoStack); undo.markSaved();
        updateDirtyFlag();
    }

    // Hooked into upstream's checkUnsavedChanges: an untitled buffer holding only whitespace
    // has nothing worth saving, so closing or replacing it does not ask. A named file still
    // asks, since emptying it and not saving would leave the old content on disk.
    bool forkIsBlankUntitled() {
        if (!currentFilePath.empty()) return false;
        size_t len = pt.length();
        for (size_t i = 0; i < len; ++i) {
            char c = pt.charAt(i);
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
        }
        return true;
    }

    // Ctrl+,: open the settings file, creating it with every setting and its default first.
    void forkOpenSettings() {
        std::wstring path = forkSettingsPath();
        if (path.empty()) return;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            CreateDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str(), NULL);
            static const char kTemplate[] =
                "; miu fork settings. Saved changes apply right away.\r\n"
                "\r\n"
                "[file]\r\n"
                "; true = save the file when you switch to another app (named files only)\r\n"
                "saveOnFocusLost=true\r\n"
                "\r\n"
                "[list]\r\n"
                "; Enter on an empty list item:\r\n"
                ";   continue = add another empty item at the same level\r\n"
                ";   outdent  = move the item one level out (at the top level, clear the marker)\r\n"
                ";   remove   = clear the marker\r\n"
                "blankItemEnter=continue\r\n";
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(h, kTemplate, (DWORD)(sizeof(kTemplate) - 1), &written, NULL);
                CloseHandle(h);
            }
        }
        if (!checkUnsavedChanges()) return;
        openFileFromPath(path);
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
        forklist::Result r = forklist::onEnter(ctx.lines, ctx.head.line, ctx.head.col, forklist::detectIndentUnit(ctx.lines), forkSettings().blankItemEnter);
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
    // Alt+Shift+Up/Down on a list item: move it with its children past its sibling.
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

    // ---- Outliner feel: the indent and marker of an item act as if they were not text.
    // Backspace at the start of an item's text: join it to the line above (fork/ListOps.h onBackspace).
    bool forkListBackspace() {
        ForkListContext ctx;
        if (!forkListContext(ctx) || cursors[0].hasSelection()) return false;
        forklist::Result r = forklist::onBackspace(ctx.lines, ctx.head.line, ctx.head.col, forklist::detectIndentUnit(ctx.lines));
        if (!r.handled) return false;
        forkApplyListResult(ctx, r);
        return true;
    }
    // Delete at the end of a list line: the next item's text joins it.
    bool forkListDelete() {
        ForkListContext ctx;
        if (!forkListContext(ctx) || cursors[0].hasSelection()) return false;
        forklist::Result r = forklist::onDelete(ctx.lines, ctx.head.line, ctx.head.col, forklist::detectIndentUnit(ctx.lines));
        if (!r.handled) return false;
        forkApplyListResult(ctx, r);
        return true;
    }
    // Where Left / Right (and the word moves) go from pos when they step over a marker:
    // from an item's text start to the end of the line above, and from a line end to the
    // text start of an item on the next line. npos when no marker is in the way.
    size_t forkListCaretStop(size_t pos, bool forward) {
        if (!forkIsListDocument() || lineStarts.empty()) return std::string::npos;
        int line = getLineIdx(pos);
        if (forward) {
            if (pos != lineContentEnd(line) || line + 1 >= (int)lineStarts.size()) return std::string::npos;
            forklist::Item next = forklist::parseItem(forkLineText(line + 1));
            return next.valid ? lineStarts[line + 1] + next.contentStart : std::string::npos;
        }
        forklist::Item it = forklist::parseItem(forkLineText(line));
        if (!it.valid || line == 0 || pos != lineStarts[line] + it.contentStart) return std::string::npos;
        return lineContentEnd(line - 1);
    }
    void forkPlaceCaret(size_t head, bool extend) {
        Cursor& c = cursors[0];
        c.head = head; c.affinity = false;
        if (!extend) c.anchor = head;
        c.desiredX = getXFromPos(head);
        ensureCaretVisible(); InvalidateRect(hwnd, NULL, FALSE);
    }
    // Left / Right over a marker. Single caret only; a selection collapses the upstream way.
    bool forkListArrow(bool forward, bool shift) {
        if (cursors.size() != 1 || isRectSelecting || (cursors[0].hasSelection() && !shift)) return false;
        size_t stop = forkListCaretStop(cursors[0].head, forward);
        if (stop == std::string::npos) return false;
        rollbackPadding();
        forkPlaceCaret(stop, shift);
        return true;
    }
    // Home on an item goes to its text start first; pressed there, upstream takes it to the line start.
    bool forkListHome(bool shift) {
        if (!forkIsListDocument() || cursors.size() != 1 || isRectSelecting || lineStarts.empty()) return false;
        size_t head = cursors[0].head;
        int line = getLineIdx(head);
        forklist::Item it = forklist::parseItem(forkLineText(line));
        if (!it.valid) return false;
        size_t textStart = lineStarts[line] + it.contentStart;
        if (head <= textStart) return false;
        // On a wrapped line, Home below the first row stays upstream's (start of that row).
        if (wordWrapEnabled) { bool aff = false; if (moveVisualHome(head, cursors[0].affinity, aff) > textStart) return false; }
        rollbackPadding();
        forkPlaceCaret(textStart, shift);
        return true;
    }
    // Called first from WM_CHAR. Returns true when the character was consumed.
    bool forkHandleChar(WPARAM ch) {
        if (ch == 13) return forkListEnter();
        // Ctrl+H also sends a backspace character; upstream ignores Ctrl characters.
        if (ch == 8 && !(GetKeyState(VK_CONTROL) & 0x8000)) return forkListBackspace();
        return false;
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
            // Ctrl+Alt adds a cursor; Alt+Shift moves a list item with its children (plain lines outside lists).
            if (vk == VK_UP || vk == VK_DOWN) {
                if (ctrl) addCursor(up);
                else if (shift && !forkListMove(up)) moveLines(up);
                return true;
            }
            if (vk == VK_F3) { selectAllOccurrences(); return true; }
            return false;
        }
        if (!ctrl && !alt) {
            switch (vk) {
            case VK_TAB: return forkListShift(shift);
            case VK_DELETE: return !shift && forkListDelete();
            case VK_LEFT: case VK_RIGHT: return forkListArrow(vk == VK_RIGHT, shift);
            case VK_HOME: return forkListHome(shift);
            default: return false;
            }
        }
        if (!ctrl) return false;
        switch (vk) {
        case VK_SPACE: if (!shift) return false; return forkListSelect();
        // At an item's text start / a list line's end these join items like Backspace / Delete.
        case VK_BACK: if (!forkListBackspace()) deleteWord(false); return true;
        case VK_DELETE: if (!forkListDelete()) deleteWord(true); return true;
        case VK_OEM_COMMA: forkOpenSettings(); return true;
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
