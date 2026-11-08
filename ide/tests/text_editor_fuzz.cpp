// Drives TextEditor with random editing sessions and checks every step against a model:
//   - the document only ever holds bytes that were put there,
//   - undo and redo return exactly the text recorded for that step,
//   - every edit can be undone,
//   - the cursor and selection stay inside the document.
//
// Build it with AddressSanitizer to catch out-of-bounds reads as well
// (cmake -DMYLO_FUZZ_SANITIZE=ON). See docs/ide.md.
//
// Usage: text_editor_fuzz [first_seed] [seed_count] [steps_per_seed]
// A failure prints its seed and last steps; `text_editor_fuzz <seed> 1` replays it.
//
// Keeping it up to date when the editor changes:
//   - New ways to edit text (a command, shortcut or completion path) go in
//     applyRandomOperation(). Call the same TextEditor method the UI calls, so the
//     test drives the real code path. Each case should note() what it did.
//   - Any bytes a new operation can insert must be marked in m_allowed, or the
//     check reports them as STRAY_BYTE.
//   - Every edit must be one undo step; the check reports anything else.
//   - The test is a friend of TextEditor and reads mLines, mState, mUndoIndex and
//     the mouse selection fields directly. If those change, update documentText(),
//     check(), mousePosition() and the MouseClick and MouseDrag cases.
//   - Adding or reordering cases changes what every seed does, so old seed
//     numbers from bug reports no longer reproduce. Note the steps, not just the seed.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

#include "imgui.h"

#include "TextEditor.h"
#include "imgui_impl_raylib.hpp"

// TextEditor only needs suggestions from the language server, so these stand in for it.
MyloLspProvider::MyloLspProvider() {}
MyloLspProvider& MyloLspProvider::instance() {
    static MyloLspProvider provider;
    return provider;
}
std::vector<LspCompletionItem> MyloLspProvider::getCompletions(const std::string&, const std::string&) const {
    std::vector<LspCompletionItem> items(3);
    items[0].label = "RL::DrawText";
    items[1].label = "apple";
    items[2].label = "b";
    return items;
}
LspSignatureHelp MyloLspProvider::getSignatureHelp(const std::string&) const { return LspSignatureHelp(); }
const char* MyloLspProvider::getKindBadge(LspCompletionKind) { return "fn"; }

ImFont* ImGui_ImplRaylib_GetMonoFont() { return nullptr; }

namespace {

// Anything else in the document came from somewhere it shouldn't have.
const char kAlphabet[] = "abc xyz(){}\"\\/:_RLDrawTextpleif=\t";

const char* const kDocuments[] = {
    "",
    "\n",
    "\n\n\n",
    "abc",
    "abc\n",
    "fn a() {\n    ret 1\n}\n",
    "a\tb\n\tc\n\t\tx\n",
    "print(\"abc\\\n\"\ny\n",
    "// a\n/* b\nc */ x\n",
    "caf\xC3\xA9 \xC3\xA9\n\xC3\xA9\n",
    "bad \xE9 x\n\xE9\nend\xE9",
    "\x80\x80 a\n\xF0\n",
    "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\n",
};

const char* const kSnippets[] = {
    "fn my_func() {\n    \n}\n",
    "if a {\n    \n} else {\n    \n}\n",
    "print(\"x\")\n",
    "abc",
    "\n",
    "a\tb",
};

std::string gClipboard;

void setClipboard(ImGuiContext*, const char* text) { gClipboard = text ? text : ""; }
const char* getClipboard(ImGuiContext*) { return gClipboard.c_str(); }

std::string normalised(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c != '\r') out += c;
    }
    return out;
}

std::string printable(const std::string& text) {
    std::string out;
    char hex[8];
    for (unsigned char c : text) {
        if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c >= 32 && c < 127) out += (char)c;
        else {
            std::snprintf(hex, sizeof(hex), "\\x%02X", c);
            out += hex;
        }
    }
    return out;
}

} // namespace

class TextEditorFuzzSession {
public:
    TextEditorFuzzSession(unsigned seed, int steps) : m_rng(seed), m_seed(seed), m_steps(steps) {
        for (const char* p = kAlphabet; *p; ++p) m_allowed[(unsigned char)*p] = true;
        m_allowed[(unsigned char)'\n'] = true;
        m_allowed[0xC3] = m_allowed[0xA9] = true;   // The typed e-acute.
        for (const char* snippet : kSnippets) {
            for (const char* p = snippet; *p; ++p) m_allowed[(unsigned char)*p] = true;
        }
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Mylo());
        loadDocument();
    }

    bool run() {
        for (int step = 0; step < m_steps; ++step) {
            std::string before = documentText(m_editor);
            int undoIndexBefore = m_editor.mUndoIndex;
            m_undid = m_redid = m_replaced = false;

            try {
                applyRandomOperation();
                m_editor.ColorizeInternal();
            } catch (const std::exception& error) {
                std::printf("seed %u step %d FAIL EXCEPTION %s\n", m_seed, step, error.what());
                for (const auto& entry : m_log) std::printf("    %s\n", entry.c_str());
                return false;
            }

            if (!check(before, undoIndexBefore)) {
                std::printf("seed %u step %d FAIL %s\n", m_seed, step, m_failure.c_str());
                for (const auto& entry : m_log) std::printf("    %s\n", entry.c_str());
                return false;
            }
        }
        return true;
    }

private:
    static std::string documentText(const TextEditor& editor) {
        std::string text;
        for (size_t l = 0; l < editor.mLines.size(); ++l) {
            if (l > 0) text += '\n';
            for (const auto& glyph : editor.mLines[l]) text += (char)glyph.mChar;
        }
        return text;
    }

    int pick(int count) { return (int)(m_rng() % (unsigned)count); }

    void note(const std::string& op) {
        auto cursor = m_editor.mState.mCursorPosition;
        char where[96];
        std::snprintf(where, sizeof(where), " @%d.%d sel %d.%d-%d.%d lines %d", cursor.mLine, cursor.mColumn,
                      m_editor.mState.mSelectionStart.mLine, m_editor.mState.mSelectionStart.mColumn,
                      m_editor.mState.mSelectionEnd.mLine, m_editor.mState.mSelectionEnd.mColumn,
                      (int)m_editor.mLines.size());
        m_log.push_back(op + where);
        if (m_log.size() > 14) m_log.pop_front();
    }

    void loadDocument() {
        const char* text = kDocuments[pick((int)(sizeof(kDocuments) / sizeof(kDocuments[0])))];
        note("SetText " + printable(text));
        m_editor.SetText(text);
        for (const char* p = text; *p; ++p) m_allowed[(unsigned char)*p] = true;
        m_replaced = true;
        m_snapshots.assign(1, documentText(m_editor));
    }

    // What a mouse click hands the editor: ScreenPosToCoordinates clamps it to the document.
    TextEditor::Coordinates mousePosition() {
        int line = pick((int)m_editor.mLines.size() + 3);
        int column = pick(line < (int)m_editor.mLines.size() ? m_editor.GetLineMaxColumn(line) + 1 : 1);
        return m_editor.SanitizeCoordinates(TextEditor::Coordinates(line, column));
    }

    void applyRandomOperation() {
        TextEditor& e = m_editor;
        bool shift = pick(3) == 0;
        bool word = pick(4) == 0;

        switch (pick(30)) {
            case 0: case 1: case 2: case 3: {
                char c = kAlphabet[pick((int)sizeof(kAlphabet) - 1)];
                note(std::string("Type ") + printable(std::string(1, c)));
                e.EnterCharacter((ImWchar)(unsigned char)c, false);
                break;
            }
            case 4: note("Type e-acute"); e.EnterCharacter((ImWchar)0xE9, false); break;
            case 5: note("Enter"); e.EnterCharacter('\n', false); break;
            case 6: note("Tab"); e.EnterCharacter('\t', false); break;
            case 7: note("ShiftTab"); e.EnterCharacter('\t', true); break;
            case 8: case 9: note("Delete"); e.Delete(); break;
            case 10: case 11: note("Backspace"); e.Backspace(); break;
            case 12: case 13: case 14: note("Undo"); m_undid = e.CanUndo(); e.Undo(); break;
            case 15: note("Redo"); m_redid = e.CanRedo(); e.Redo(); break;
            case 16: note("Up"); e.MoveUp(1 + pick(3), shift); break;
            case 17: note("Down"); e.MoveDown(1 + pick(3), shift); break;
            case 18: note("Left"); e.MoveLeft(1 + pick(3), shift, word); break;
            case 19: note("Right"); e.MoveRight(1 + pick(3), shift, word); break;
            case 20:
                note("HomeEnd");
                if (pick(2)) e.MoveHome(shift); else e.MoveEnd(shift);
                break;
            case 21:
                note("TopBottom");
                if (pick(2)) e.MoveTop(shift); else e.MoveBottom(shift);
                break;
            case 22: note("SelectAll"); e.SelectAll(); break;
            case 23: {
                note("MouseClick");
                auto mode = (TextEditor::SelectionMode)pick(3);
                e.mState.mCursorPosition = e.mInteractiveStart = e.mInteractiveEnd = mousePosition();
                e.SetSelection(e.mInteractiveStart, e.mInteractiveEnd, mode);
                break;
            }
            case 24:
                note("MouseDrag");
                e.mState.mCursorPosition = e.mInteractiveEnd = mousePosition();
                e.SetSelection(e.mInteractiveStart, e.mInteractiveEnd);
                break;
            case 25: note("Cut"); e.Cut(); break;
            case 26: note("Copy"); e.Copy(); break;
            case 27: note("Paste"); e.Paste(); break;
            case 28:
                if (pick(2)) {
                    const char* snippet = kSnippets[pick((int)(sizeof(kSnippets) / sizeof(kSnippets[0])))];
                    note("InsertText " + printable(snippet));
                    e.InsertText(snippet);
                } else {
                    note("ApplySuggestion");
                    e.TriggerAutocomplete();
                    e.ApplyAutocomplete(pick((int)e.mSuggestions.size() + 1));
                }
                break;
            case 29:
                switch (pick(8)) {
                    case 0: note("Overwrite"); e.mOverwrite = !e.mOverwrite; break;
                    case 1: note("SwitchScript"); loadDocument(); break;
                    default: note("SelectWord"); e.SelectWordUnderCursor(); break;
                }
                break;
        }
    }

    bool fail(const std::string& message) {
        m_failure = message;
        return false;
    }

    bool check(const std::string& before, int undoIndexBefore) {
        const TextEditor& e = m_editor;
        std::string text = documentText(e);

        for (unsigned char c : text) {
            if (!m_allowed[c]) return fail("STRAY_BYTE " + printable(text));
        }

        int undoIndex = e.mUndoIndex;
        if (m_undid) {
            if (undoIndex != undoIndexBefore - 1) return fail("UNDO_INDEX");
            if (text != m_snapshots[undoIndex]) {
                return fail("UNDO_MISMATCH got '" + printable(text) + "' want '" + printable(m_snapshots[undoIndex]) + "'");
            }
        } else if (m_redid) {
            if (undoIndex != undoIndexBefore + 1) return fail("REDO_INDEX");
            if (text != m_snapshots[undoIndex]) {
                return fail("REDO_MISMATCH got '" + printable(text) + "' want '" + printable(m_snapshots[undoIndex]) + "'");
            }
        } else if (!m_replaced) {
            if (undoIndex == undoIndexBefore + 1) {
                m_snapshots.resize((size_t)undoIndex);
                m_snapshots.push_back(text);
            } else if (undoIndex != undoIndexBefore) {
                return fail("UNDO_JUMP");
            } else if (text != before) {
                return fail("EDIT_WITHOUT_UNDO");
            }
        }

        if (e.mLines.empty()) return fail("NO_LINES");

        auto inDocument = [&](const TextEditor::Coordinates& c) {
            return c.mLine >= 0 && c.mLine < (int)e.mLines.size() && c.mColumn >= 0 &&
                   c.mColumn <= e.GetLineMaxColumn(c.mLine);
        };
        // Moving up and down keeps the column past a short line's end, so only the line must be valid.
        if (!inDocument(e.GetActualCursorCoordinates()) || e.mState.mCursorPosition.mLine >= (int)e.mLines.size()) {
            return fail("CURSOR_OUTSIDE_DOCUMENT");
        }
        if (!inDocument(e.mState.mSelectionStart) || !inDocument(e.mState.mSelectionEnd)) return fail("SELECTION_OUTSIDE_DOCUMENT");
        if (e.mState.mSelectionStart > e.mState.mSelectionEnd) return fail("SELECTION_REVERSED");

        std::string fromEditor = e.GetText();
        if (fromEditor != text) {
            return fail("GETTEXT_MISMATCH got '" + printable(fromEditor) + "' want '" + printable(text) + "'");
        }
        return true;
    }

    std::mt19937 m_rng;
    unsigned m_seed;
    int m_steps;
    TextEditor m_editor;
    std::vector<std::string> m_snapshots;
    std::deque<std::string> m_log;
    std::string m_failure;
    std::array<bool, 256> m_allowed{};
    bool m_undid = false;
    bool m_redid = false;
    bool m_replaced = false;
};

int main(int argc, char** argv) {
    unsigned firstSeed = argc > 1 ? (unsigned)std::strtoul(argv[1], nullptr, 10) : 1;
    int seedCount = argc > 2 ? std::atoi(argv[2]) : 300;
    int steps = argc > 3 ? std::atoi(argv[3]) : 400;

#ifdef _MSC_VER
    // A failed assert should fail the run, not wait on a dialog.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    ImGui::CreateContext();
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Platform_SetClipboardTextFn = setClipboard;
    platform.Platform_GetClipboardTextFn = getClipboard;

    int failures = 0;
    for (int i = 0; i < seedCount; ++i) {
        gClipboard.clear();
        TextEditorFuzzSession session(firstSeed + (unsigned)i, steps);
        if (!session.run()) ++failures;
    }

    ImGui::DestroyContext();
    std::printf("%d of %d sessions failed\n", failures, seedCount);
    return failures == 0 ? 0 : 1;
}
