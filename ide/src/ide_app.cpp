#include "ide_app.hpp"

#include "imgui.h"
#include "imgui_impl_raylib.hpp"
#include "imgui_internal.h"
#include "mylo_lsp.hpp"
#include "raylib.h"

extern "C" {
#include "version.h"
#include "vm.h"
}

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const double kDiagnosticsDelay = 0.3;   // Seconds after the last edit, so typing stays smooth.
const int kMaxRecent = 10;
const int kPageSize = 256;

const ImVec4 kErrorColor(1.0f, 0.45f, 0.42f, 1.0f);
const ImVec4 kInfoColor(0.55f, 0.62f, 0.72f, 1.0f);
const ImVec4 kInputColor(0.45f, 0.8f, 0.95f, 1.0f);
const ImVec4 kWarnColor(1.0f, 0.75f, 0.3f, 1.0f);
const ImVec4 kGoodColor(0.45f, 0.85f, 0.5f, 1.0f);

bool readFile(const std::string& path, std::string& outText) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    outText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

std::string withLineEnding(const std::string& text, bool crlf) {
    if (!crlf) return text;
    std::string out;
    out.reserve(text.size() + text.size() / 32);
    for (char c : text) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

std::string absolutePath(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(path), ec);
    if (ec) return path;
    return p.lexically_normal().string();
}

// Compares two paths as the same file, even if one doesn't exist yet.
bool samePath(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec)) return fs::equivalent(a, b, ec);
    std::string x = absolutePath(a), y = absolutePath(b);
#ifdef _WIN32
    std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return (char)std::tolower(c == '/' ? '\\' : c); });
    std::transform(y.begin(), y.end(), y.begin(), [](unsigned char c) { return (char)std::tolower(c == '/' ? '\\' : c); });
#endif
    return x == y;
}

std::string fileName(const std::string& path) { return fs::path(path).filename().string(); }

std::string parentDir(const std::string& path) {
    if (path.empty()) return "";
    return fs::path(absolutePath(path)).parent_path().string();
}

// Finds "path:line:" in console text, so clicking an error opens its line.
bool findLocation(const std::string& text, std::string& outPath, int& outLine) {
    for (size_t colon = text.find(':'); colon != std::string::npos; colon = text.find(':', colon + 1)) {
        size_t digits = colon + 1;
        while (digits < text.size() && std::isdigit((unsigned char)text[digits])) ++digits;
        if (digits == colon + 1 || digits >= text.size() || text[digits] != ':') continue;
        std::string path = text.substr(0, colon);
        size_t start = path.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        path = path.substr(start);
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) {
            outPath = path;
            outLine = std::atoi(text.c_str() + colon + 1);
            return true;
        }
    }
    return false;
}

std::string formatCount(long long n) {
    std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) out += ',';
        out += *it;
        ++count;
    }
    if (n < 0) out += '-';
    std::reverse(out.begin(), out.end());
    return out;
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// A usage bar that turns amber, then red, as it nears its limit.
void usageBar(const char* label, long long used, long long limit, const char* hint) {
    float fraction = limit > 0 ? (float)((double)used / (double)limit) : 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    ImVec4 color = fraction > 0.9f ? kErrorColor : fraction > 0.75f ? kWarnColor : kGoodColor;
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
    std::string overlay = formatCount(used) + " / " + formatCount(limit);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay.c_str());
    ImGui::PopStyleColor();
    if (hint && fraction > 0.75f && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
}

std::set<int> toSet(const TextEditor::Breakpoints& b) { return std::set<int>(b.begin(), b.end()); }
TextEditor::Breakpoints toEditor(const std::set<int>& s) { return TextEditor::Breakpoints(s.begin(), s.end()); }

std::string defaultConfigPath(const std::string& dir) {
    return (fs::path(dir) / MYLO_VM_CONF_NAME).string();
}

} // namespace

std::string IdeDocument::name() const { return path.empty() ? "untitled" : fileName(path); }

// ---------------------------------------------------------------------------
// Setup and settings
// ---------------------------------------------------------------------------

IdeApp::IdeApp(std::string configDir) : m_configDir(std::move(configDir)) {
    std::error_code ec;
    fs::create_directories(m_configDir, ec);
    m_layoutPath = (fs::path(m_configDir) / "layout.ini").string();
    m_settingsPath = (fs::path(m_configDir) / "settings.ini").string();
    mylo_vm_options_defaults(&m_vmOptions);
    m_vmOptionsSaved = m_vmOptions;
    loadSettings();

    // The completions look for modules beside mylo, as the compiler does.
    std::string myloDir = parentDir(myloExecutable());
    if (!myloDir.empty()) std::snprintf(mylo_exe_dir, sizeof(mylo_exe_dir), "%s", myloDir.c_str());

    m_session.onCompileError = [this](const std::string& file, int line, const std::string& message) {
        std::string path = file.empty() ? m_runScript : file;
        if (IdeDocument* doc = findDocumentByPath(path)) {
            TextEditor::ErrorMarkers markers;
            markers[line] = message;
            doc->editor->SetErrorMarkers(markers);
        }
        goToLine(path, line);
        m_status = "The script doesn't compile: " + message;
    };
}

IdeApp::~IdeApp() {
    m_session.stop();
    saveSettings();
}

void IdeApp::loadSettings() {
    std::ifstream in(m_settingsPath);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "mylo") m_settings.myloPath = value;
        else if (key == "font_size") m_settings.fontSize = std::clamp((float)std::atof(value.c_str()), 10.0f, 40.0f);
        else if (key == "save_before_run") m_settings.saveBeforeRun = value == "1";
        else if (key == "recent") m_settings.recentFiles.push_back(value);
        else if (key == "breakpoint") {
            size_t bar = value.rfind('|');
            if (bar != std::string::npos) m_settings.breakpoints[value.substr(0, bar)].insert(std::atoi(value.c_str() + bar + 1));
        }
    }
}

void IdeApp::saveSettings() {
    // Breakpoints in open documents live in their editors; gather them first.
    for (const auto& doc : m_documents) {
        if (!doc->path.empty()) m_settings.breakpoints[doc->path] = toSet(doc->editor->GetBreakpoints());
    }
    std::ofstream out(m_settingsPath, std::ios::trunc);
    out << "mylo=" << m_settings.myloPath << "\n";
    out << "font_size=" << m_settings.fontSize << "\n";
    out << "save_before_run=" << (m_settings.saveBeforeRun ? 1 : 0) << "\n";
    for (const auto& r : m_settings.recentFiles) out << "recent=" << r << "\n";
    for (const auto& entry : m_settings.breakpoints) {
        for (int line : entry.second) out << "breakpoint=" << entry.first << "|" << line << "\n";
    }
}

std::string IdeApp::myloExecutable() const {
    if (!m_settings.myloPath.empty()) return m_settings.myloPath;
#ifdef _WIN32
    const char* name = "mylo.exe";
#else
    const char* name = "mylo";
#endif
    std::error_code ec;
    fs::path exeDir = fs::path(GetApplicationDirectory());
    fs::path beside = exeDir / name;
    if (fs::exists(beside, ec)) return beside.string();
    // In a build tree the IDE is in ide/, a folder below mylo.
    fs::path above = (exeDir / ".." / name).lexically_normal();
    if (fs::exists(above, ec)) return above.string();
    return name;   // let the system find it on PATH
}

// ---------------------------------------------------------------------------
// Documents
// ---------------------------------------------------------------------------

IdeDocument& IdeApp::addDocument(const std::string& path) {
    auto doc = std::make_unique<IdeDocument>();
    doc->id = m_nextDocumentId++;
    doc->path = path;
    doc->editor = std::make_unique<TextEditor>();
    doc->editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Mylo());
    auto saved = m_settings.breakpoints.find(path);
    if (!path.empty() && saved != m_settings.breakpoints.end()) doc->editor->SetBreakpoints(toEditor(saved->second));
    m_selectDocumentId = doc->id;
    m_documents.push_back(std::move(doc));
    return *m_documents.back();
}

IdeDocument* IdeApp::findDocument(int id) {
    for (auto& doc : m_documents) {
        if (doc->id == id) return doc.get();
    }
    return nullptr;
}

IdeDocument* IdeApp::findDocumentByPath(const std::string& path) {
    for (auto& doc : m_documents) {
        if (samePath(doc->path, path)) return doc.get();
    }
    return nullptr;
}

void IdeApp::openDocument(const std::string& rawPath) {
    std::string path = absolutePath(rawPath);
    if (IdeDocument* existing = findDocumentByPath(path)) {
        m_selectDocumentId = existing->id;
        return;
    }
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        m_status = "'" + path + "' is a folder, not a script";
        return;
    }
    std::string text;
    bool exists = fs::exists(path, ec);
    if (exists && !readFile(path, text)) {
        m_status = "Could not read '" + path + "'";
        return;
    }

    // Replace a lone, empty untitled document rather than keeping it beside the new one.
    if (m_documents.size() == 1 && m_documents[0]->path.empty() && !m_documents[0]->modified &&
        m_documents[0]->editor->GetText().empty()) {
        m_documents.clear();
    }

    IdeDocument& doc = addDocument(path);
    doc.crlf = text.find("\r\n") != std::string::npos;
    doc.editor->SetText(text);
    doc.savedText = doc.editor->GetText();
    addRecent(path);
    m_status = exists ? "Opened " + doc.name() : doc.name() + " is new, and is created when saved";
}

void IdeApp::newDocument() { addDocument(""); }

void IdeApp::addRecent(const std::string& path) {
    auto& recent = m_settings.recentFiles;
    recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const std::string& r) { return samePath(r, path); }), recent.end());
    recent.insert(recent.begin(), path);
    if ((int)recent.size() > kMaxRecent) recent.resize(kMaxRecent);
}

bool IdeApp::save(IdeDocument& doc) {
    if (doc.path.empty()) {
        beginBrowse(BrowseMode::SaveAs, doc.id);
        return false;
    }
    std::string text = doc.editor->GetText();
    std::string onDisk = withLineEnding(text, doc.crlf);
    std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
    out.write(onDisk.data(), (std::streamsize)onDisk.size());
    out.close();
    if (!out) {
        m_status = "Could not save '" + doc.path + "'";
        return false;
    }
    doc.savedText = text;
    doc.modified = false;
    m_status = "Saved " + doc.name();
    return true;
}

bool IdeApp::saveAll() {
    bool ok = true;
    for (auto& doc : m_documents) {
        if (doc->modified && !doc->path.empty()) ok = save(*doc) && ok;
    }
    return ok;
}

void IdeApp::removeDocument(int id) {
    IdeDocument* doc = findDocument(id);
    if (doc && !doc->path.empty()) m_settings.breakpoints[doc->path] = toSet(doc->editor->GetBreakpoints());
    m_documents.erase(std::remove_if(m_documents.begin(), m_documents.end(),
                                     [&](const std::unique_ptr<IdeDocument>& d) { return d->id == id; }),
                      m_documents.end());
    if (m_analyzedDocumentId == id) m_analyzedDocumentId = -1;
    if (m_documents.empty() && !m_quitting) newDocument();
}

void IdeApp::requestClose(int id) {
    IdeDocument* doc = findDocument(id);
    if (!doc) return;
    if (doc->modified) {
        m_unsavedDocumentId = id;
        m_openUnsavedPopup = true;
    } else {
        removeDocument(id);
    }
}

void IdeApp::requestQuit() {
    m_quitting = true;
    continueQuit();
}

void IdeApp::continueQuit() {
    for (auto& doc : m_documents) {
        if (doc->modified) {
            m_selectDocumentId = doc->id;
            m_unsavedDocumentId = doc->id;
            m_openUnsavedPopup = true;
            return;
        }
    }
    m_session.stop();
    m_done = true;
}

void IdeApp::finishUnsaved(int id) {
    m_unsavedDocumentId = -1;
    if (m_quitting) continueQuit();
    else removeDocument(id);
}

void IdeApp::goToLine(const std::string& path, int line) {
    if (path.empty()) return;
    IdeDocument* doc = findDocumentByPath(path);
    if (!doc) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) return;
        openDocument(path);
        doc = findDocumentByPath(path);
        if (!doc) return;
    }
    m_selectDocumentId = doc->id;
    if (line > 0) {
        doc->editor->SetCursorPosition(TextEditor::Coordinates(line - 1, 0));
        doc->editor->ScrollToLine(line);
    }
    m_focusEditor = true;
}

// ---------------------------------------------------------------------------
// Running and debugging
// ---------------------------------------------------------------------------

std::map<std::string, std::set<int>> IdeApp::allBreakpoints() const {
    std::map<std::string, std::set<int>> all;
    for (const auto& entry : m_settings.breakpoints) {
        if (!entry.second.empty()) all[entry.first] = entry.second;
    }
    for (const auto& doc : m_documents) {
        if (doc->path.empty()) continue;
        if (doc->editor->GetBreakpoints().empty()) all.erase(doc->path);
        else all[doc->path] = toSet(doc->editor->GetBreakpoints());
    }
    return all;
}

void IdeApp::syncBreakpoints() {
    for (auto& doc : m_documents) {
        if (!doc->editor->ConsumeBreakpointsChanged() || doc->path.empty()) continue;
        m_settings.breakpoints[doc->path] = toSet(doc->editor->GetBreakpoints());
        m_session.setBreakpoints(doc->path, m_settings.breakpoints[doc->path]);
    }
}

void IdeApp::toggleBreakpointAtCursor() {
    IdeDocument* doc = activeDocument();
    if (!doc) return;
    int line = doc->editor->GetCursorPosition().mLine + 1;
    TextEditor::Breakpoints bps = doc->editor->GetBreakpoints();
    if (bps.count(line)) bps.erase(line);
    else bps.insert(line);
    doc->editor->SetBreakpoints(bps);
    if (!doc->path.empty()) {
        m_settings.breakpoints[doc->path] = toSet(bps);
        m_session.setBreakpoints(doc->path, m_settings.breakpoints[doc->path]);
    }
}

void IdeApp::removeAllBreakpoints() {
    for (const auto& entry : allBreakpoints()) m_session.setBreakpoints(entry.first, {});
    m_settings.breakpoints.clear();
    for (auto& doc : m_documents) doc->editor->SetBreakpoints({});
}

void IdeApp::start(bool debug) {
    IdeDocument* doc = activeDocument();
    if (!doc) return;
    if (doc->path.empty()) {
        // An untitled script needs a name before mylo can run it.
        m_afterSave = debug ? PendingAction::Debug : PendingAction::Run;
        beginBrowse(BrowseMode::SaveAs, doc->id);
        return;
    }
    if (m_settings.saveBeforeRun) saveAll();
    else if (doc->modified) save(*doc);

    if (m_session.isActive()) m_session.stop();
    m_session.clearConsole();

    LaunchOptions options;
    options.myloPath = myloExecutable();
    options.scriptPath = doc->path;
    options.workingDir = parentDir(doc->path);

    // Settings come from the same place mylo would find them: beside the script, then beside mylo.
    char found[MYLO_VM_PATH_LENGTH * 2];
    std::string myloDir = parentDir(options.myloPath);
    if (mylo_vm_options_find(found, sizeof(found), options.workingDir.c_str(), myloDir.empty() ? nullptr : myloDir.c_str())) {
        options.configPath = found;
        m_session.addInfo(std::string("Using VM settings from ") + found);
    }

    for (auto& d : m_documents) d->editor->SetErrorMarkers({});

    std::string error;
    bool ok = debug ? m_session.debug(options, allBreakpoints(), error) : m_session.run(options, error);
    if (!ok) {
        m_session.addInfo("Could not start mylo: " + error);
        m_session.addInfo("Set the mylo executable in Settings (it's looked for beside the IDE, then on PATH).");
        m_status = "Could not start mylo";
    } else {
        m_status = debug ? "Debugging " + doc->name() : "Running " + doc->name();
    }
    m_runScript = doc->path;
    m_lastRunWasDebug = debug;
    m_focusConsole = true;
}

void IdeApp::stopProgram() {
    if (!m_session.isActive()) return;
    m_session.stop();
    m_status = "Stopped";
}

void IdeApp::restartProgram() {
    if (m_runScript.empty()) return;
    bool debug = m_lastRunWasDebug;
    m_session.stop();
    if (IdeDocument* doc = findDocumentByPath(m_runScript)) m_selectDocumentId = doc->id, m_activeDocumentId = doc->id;
    start(debug);
}

void IdeApp::showStopLocation() {
    const auto& frames = m_session.frames();
    if (frames.empty()) return;
    const StackFrameInfo& frame = frames[(size_t)m_session.selectedFrame()];
    goToLine(frame.path, frame.line);
    m_scrollToIp = true;
    if (m_session.stopReason() == "exception") m_status = "Stopped on a runtime error: " + m_session.stopText();
    else m_status = "Paused at " + fileName(frame.path) + ":" + std::to_string(frame.line);
}

void IdeApp::updateExecutionMarkers() {
    const auto& frames = m_session.frames();
    const StackFrameInfo* frame = (m_session.isPaused() && !frames.empty()) ? &frames[(size_t)m_session.selectedFrame()] : nullptr;
    for (auto& doc : m_documents) {
        if (frame && samePath(doc->path, frame->path)) doc->editor->SetExecutionLine(frame->line, m_session.selectedFrame() == 0);
        else doc->editor->SetExecutionLine(0);
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void IdeApp::update() {
    m_session.memoryWanted = false;
    DebugSession::State before = m_session.state();
    m_session.update();
    if (m_session.consumeStopped()) showStopLocation();
    if (before != DebugSession::State::Exited && m_session.state() == DebugSession::State::Exited) {
        m_status = m_session.exitCode() == 0 ? "Finished" : "Exited with code " + std::to_string(m_session.exitCode());
    } else if (before == DebugSession::State::Paused && m_session.state() == DebugSession::State::Running) {
        m_status = m_session.isDebugging() ? "Running" : m_status;
    }

    ImGui::GetStyle().FontSizeBase = m_settings.fontSize;

    renderMenuBar();
    renderToolbar();
    renderStatusBar();

    ImGuiID dockspace = ImGui::DockSpaceOverViewport(ImGui::GetID("MainDock"), ImGui::GetMainViewport());
    if (!m_layoutBuilt) {
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
        if (!node || !node->IsSplitNode()) buildDefaultLayout(dockspace);
        m_layoutBuilt = true;
    }

    handleShortcuts();
    renderEditorPanel();
    renderConsolePanel();
    renderVariablesPanel();
    renderWatchPanel();
    renderCallStackPanel();
    renderBreakpointsPanel();
    renderMemoryPanel();
    renderVmOptionsPanel();

    renderBrowsePopup();
    renderUnsavedPopup();
    renderSettingsPopup();
    renderAboutPopup();

    syncBreakpoints();
    updateExecutionMarkers();
    updateDiagnostics();
    updateWindowTitle();
    runAutomation();
}

// Runs automation commands, one or more per frame:
//   frames N         wait N frames          shot FILE        save a screenshot
//   run | debug      start the script       waitpaused       wait until it pauses (10 s)
//   continue | stepover | stepin | stepout | pause | stop    waitexit   wait until it ends
//   breakpoint N     toggle a breakpoint    focus PANEL      bring a panel to the front
//   frame N          select a call frame    watch EXPR       add a watch expression
//   type TEXT        send a console line    quit             close the IDE
//   memtab NAME      show a Memory tab      open FILE        open a script
void IdeApp::runAutomation() {
    if (m_automationStep >= m_automation.size()) return;
    if (m_automationWait > 0) { m_automationWait--; return; }
    while (m_automationStep < m_automation.size()) {
        const std::string& line = m_automation[m_automationStep];
        std::string cmd = line, arg;
        size_t space = line.find(' ');
        if (space != std::string::npos) { cmd = line.substr(0, space); arg = line.substr(space + 1); }

        if (cmd == "waitpaused" || cmd == "waitexit") {
            if (m_automationDeadline == 0.0) m_automationDeadline = GetTime() + 10.0;
            bool done = cmd == "waitpaused" ? (m_session.isPaused() && !m_session.frames().empty())
                                            : m_session.state() == DebugSession::State::Exited;
            if (!done && GetTime() < m_automationDeadline) return;
            if (!done) {
                std::fprintf(stderr, "automation: timed out on '%s'\n", line.c_str());
                m_automationFailed = true;
            }
            m_automationDeadline = 0.0;
            m_automationStep++;
            m_automationWait = 3;   // let the panels catch up
            return;
        }
        m_automationStep++;
        if (cmd == "frames") { m_automationWait = std::atoi(arg.c_str()); return; }
        if (cmd == "shot") { m_screenshotPath = arg; m_automationWait = 1; return; }
        if (cmd == "run") start(false);
        else if (cmd == "debug") start(true);
        else if (cmd == "continue") m_session.continueRunning();
        else if (cmd == "stepover") m_session.stepOver();
        else if (cmd == "stepin") m_session.stepIn();
        else if (cmd == "stepout") m_session.stepOut();
        else if (cmd == "pause") m_session.pause();
        else if (cmd == "stop") stopProgram();
        else if (cmd == "frame") m_session.selectFrame(std::atoi(arg.c_str()));
        else if (cmd == "watch") { m_session.watches().push_back(arg); m_session.refreshWatches(); }
        else if (cmd == "type") m_session.sendInput(arg);
        else if (cmd == "focus") ImGui::SetWindowFocus(arg.c_str());
        else if (cmd == "memtab") { ImGui::SetWindowFocus("Memory"); m_selectMemoryTab = arg; }
        else if (cmd == "open") openDocument(arg);
        else if (cmd == "breakpoint") {
            if (IdeDocument* doc = activeDocument()) {
                doc->editor->SetCursorPosition(TextEditor::Coordinates(std::atoi(arg.c_str()) - 1, 0));
                toggleBreakpointAtCursor();
            }
        }
        else if (cmd == "quit") { m_session.stop(); m_done = true; }
        else {
            std::fprintf(stderr, "automation: unknown command '%s'\n", line.c_str());
            m_automationFailed = true;
        }
        m_automationWait = 1;
        return;
    }
}

void IdeApp::buildDefaultLayout(unsigned int dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID main = dockspaceId;
    ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.30f, nullptr, &main);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.30f, nullptr, &main);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.40f, nullptr, &right);

    ImGui::DockBuilderDockWindow("Editor", main);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Memory", bottom);
    ImGui::DockBuilderDockWindow("Variables", right);
    ImGui::DockBuilderDockWindow("Watch", right);
    ImGui::DockBuilderDockWindow("VM Options", right);
    ImGui::DockBuilderDockWindow("Call Stack", rightBottom);
    ImGui::DockBuilderDockWindow("Breakpoints", rightBottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

void IdeApp::renderMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    IdeDocument* doc = activeDocument();
    bool active = m_session.isActive();
    bool paused = m_session.isPaused();

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) newDocument();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) beginBrowse(BrowseMode::Open, -1);
        if (ImGui::BeginMenu("Open Recent", !m_settings.recentFiles.empty())) {
            std::string chosen;
            for (const auto& r : m_settings.recentFiles) {
                if (ImGui::MenuItem(r.c_str())) chosen = r;
            }
            ImGui::EndMenu();
            if (!chosen.empty()) openDocument(chosen);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, doc != nullptr)) save(*doc);
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, doc != nullptr)) beginBrowse(BrowseMode::SaveAs, doc->id);
        if (ImGui::MenuItem("Save All")) saveAll();
        ImGui::Separator();
        if (ImGui::MenuItem("Close Tab", "Ctrl+W", false, doc != nullptr)) requestClose(doc->id);
        if (ImGui::MenuItem("Settings...")) m_openSettingsPopup = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        bool has = doc != nullptr;
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, has && doc->editor->CanUndo())) doc->editor->Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, has && doc->editor->CanRedo())) doc->editor->Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, has)) doc->editor->Cut();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, has)) doc->editor->Copy();
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, has)) doc->editor->Paste();
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", "Ctrl+A", false, has)) doc->editor->SelectAll();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Run")) {
        if (ImGui::MenuItem(paused ? "Continue" : "Start Debugging", "F5", false, !active || paused)) {
            if (paused) m_session.continueRunning();
            else start(true);
        }
        if (ImGui::MenuItem("Run Without Debugging", "Ctrl+F5", false, !active)) start(false);
        if (ImGui::MenuItem("Pause", "F6", false, m_session.isDebugging() && !paused)) m_session.pause();
        if (ImGui::MenuItem("Stop", "Shift+F5", false, active)) stopProgram();
        if (ImGui::MenuItem("Restart", "Ctrl+Shift+F5", false, !m_runScript.empty())) restartProgram();
        ImGui::Separator();
        if (ImGui::MenuItem("Step Over", "F10", false, paused)) m_session.stepOver();
        if (ImGui::MenuItem("Step Into", "F11", false, paused)) m_session.stepIn();
        if (ImGui::MenuItem("Step Out", "Shift+F11", false, paused)) m_session.stepOut();
        ImGui::Separator();
        if (ImGui::MenuItem("Toggle Breakpoint", "F9", false, doc != nullptr)) toggleBreakpointAtCursor();
        if (ImGui::MenuItem("Remove All Breakpoints")) removeAllBreakpoints();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        const char* panels[] = {"Editor", "Console", "Variables", "Watch", "Call Stack", "Breakpoints", "Memory", "VM Options"};
        for (const char* name : panels) {
            if (ImGui::MenuItem(name)) ImGui::SetWindowFocus(name);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) m_layoutBuilt = false, ImGui::DockBuilderRemoveNode(ImGui::GetID("MainDock"));
        if (ImGui::MenuItem("Larger Text", "Ctrl+=")) m_settings.fontSize = std::min(40.0f, m_settings.fontSize + 1.0f);
        if (ImGui::MenuItem("Smaller Text", "Ctrl+-")) m_settings.fontSize = std::max(10.0f, m_settings.fontSize - 1.0f);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About Mylo IDE")) m_openAboutPopup = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void IdeApp::renderToolbar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::BeginViewportSideBar("##Toolbar", viewport, ImGuiDir_Up, height, flags)) {
        ImGui::End();
        return;
    }
    bool active = m_session.isActive();
    bool paused = m_session.isPaused();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetFontSize() * 0.6f, ImGui::GetStyle().FramePadding.y * 0.5f));

    auto button = [](const char* label, bool enabled, const char* tip) {
        ImGui::BeginDisabled(!enabled);
        bool pressed = ImGui::Button(label);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
        return pressed;
    };

    if (button(paused ? "Continue" : "Debug", !active || paused, paused ? "Continue (F5)" : "Start debugging (F5)")) {
        if (paused) m_session.continueRunning();
        else start(true);
    }
    if (button("Run", !active, "Run without debugging (Ctrl+F5)")) start(false);
    if (button("Pause", m_session.isDebugging() && !paused, "Pause (F6)")) m_session.pause();
    if (button("Stop", active, "Stop (Shift+F5)")) stopProgram();
    if (button("Restart", !m_runScript.empty(), "Restart (Ctrl+Shift+F5)")) restartProgram();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (button("Step Over", paused, "Step over (F10)")) m_session.stepOver();
    if (button("Step Into", paused, "Step into (F11)")) m_session.stepIn();
    if (button("Step Out", paused, "Step out (Shift+F11)")) m_session.stepOut();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // What the program is doing now.
    switch (m_session.state()) {
        case DebugSession::State::Idle: ImGui::TextDisabled("Ready"); break;
        case DebugSession::State::Running:
            ImGui::TextColored(kGoodColor, m_session.isDebugging() ? "Debugging - running" : "Running");
            break;
        case DebugSession::State::Paused:
            ImGui::TextColored(m_session.stopReason() == "exception" ? kErrorColor : kWarnColor,
                               m_session.stopReason() == "exception" ? "Stopped on an error" : "Paused");
            break;
        case DebugSession::State::Exited:
            if (m_session.exitCode() == 0) ImGui::TextDisabled("Finished");
            else ImGui::TextColored(kErrorColor, "Exited with code %d", m_session.exitCode());
            break;
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

void IdeApp::renderStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float height = ImGui::GetFrameHeight();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, 2.0f));
    if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags)) {
        IdeDocument* doc = activeDocument();
        if (doc) {
            auto cursor = doc->editor->GetCursorPosition();
            ImGui::Text("Ln %d, Col %d | %s |", cursor.mLine + 1, cursor.mColumn + 1, doc->crlf ? "CRLF" : "LF");
            ImGui::SameLine();
            const auto& diagnostics = MyloLspProvider::instance().getDiagnostics();
            if (m_analyzedDocumentId == doc->id && !diagnostics.empty()) {
                const auto& d = diagnostics[0];
                if (d.file.empty()) ImGui::TextColored(kErrorColor, "Line %d: %s |", d.line, d.message.c_str());
                else ImGui::TextColored(kErrorColor, "%s:%d: %s |", fileName(d.file).c_str(), d.line, d.message.c_str());
            } else if (!m_status.empty()) {
                ImGui::Text("%s |", m_status.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", doc->path.empty() ? "untitled" : doc->path.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void IdeApp::handleShortcuts() {
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    const ImGuiIO& io = ImGui::GetIO();
    IdeDocument* doc = activeDocument();
    bool paused = m_session.isPaused();

    // Function keys work wherever the focus is.
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        if (io.KeyCtrl && io.KeyShift) restartProgram();
        else if (io.KeyShift) stopProgram();
        else if (io.KeyCtrl) { if (!m_session.isActive()) start(false); }
        else if (paused) m_session.continueRunning();
        else if (!m_session.isActive()) start(true);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) m_session.pause();
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) toggleBreakpointAtCursor();
    if (ImGui::IsKeyPressed(ImGuiKey_F10, false) && paused) m_session.stepOver();
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false) && paused) {
        if (io.KeyShift) m_session.stepOut();
        else m_session.stepIn();
    }

    if (!io.KeyCtrl || io.KeyAlt) return;
    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) newDocument();
    else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) beginBrowse(BrowseMode::Open, -1);
    else if (doc && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift) beginBrowse(BrowseMode::SaveAs, doc->id);
        else save(*doc);
    }
    else if (doc && ImGui::IsKeyPressed(ImGuiKey_W, false)) requestClose(doc->id);
    else if (ImGui::IsKeyPressed(ImGuiKey_Equal, false)) m_settings.fontSize = std::min(40.0f, m_settings.fontSize + 1.0f);
    else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false)) m_settings.fontSize = std::max(10.0f, m_settings.fontSize - 1.0f);
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

void IdeApp::renderEditorPanel() {
    if (m_focusEditor) {
        ImGui::SetNextWindowFocus();
        m_focusEditor = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    bool visible = ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##Documents", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
                                              ImGuiTabBarFlags_FittingPolicyScroll)) {
        int closeId = -1;
        for (auto& doc : m_documents) {
            std::string label = doc->name() + "###doc" + std::to_string(doc->id);
            ImGuiTabItemFlags flags = doc->modified ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
            if (m_selectDocumentId == doc->id) flags |= ImGuiTabItemFlags_SetSelected;
            bool open = true;
            if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                m_activeDocumentId = doc->id;
                if (ImGui::IsItemHovered() && !doc->path.empty()) ImGui::SetTooltip("%s", doc->path.c_str());
                doc->editor->Render("##Editor", ImGui::GetContentRegionAvail(), false);
                if (doc->editor->IsTextChanged()) {
                    doc->modified = doc->editor->GetText() != doc->savedText;
                    doc->diagnosticsDue = true;
                    doc->lastEditTime = ImGui::GetTime();
                }
                ImGui::EndTabItem();
            }
            if (!open) closeId = doc->id;
        }
        m_selectDocumentId = -1;
        ImGui::EndTabBar();
        if (closeId != -1) requestClose(closeId);
    }
    ImGui::End();
}

void IdeApp::updateDiagnostics() {
    IdeDocument* doc = activeDocument();
    if (!doc) return;
    bool switched = m_analyzedDocumentId != doc->id;
    bool settled = doc->diagnosticsDue && ImGui::GetTime() - doc->lastEditTime >= kDiagnosticsDelay;
    if (!switched && !settled) return;

    // Imports resolve with the settings the script runs with, such as its module paths.
    loadEffectiveOptions();
    MyloVMOptions saved = MyloVMOpts;
    char found[MYLO_VM_PATH_LENGTH * 2];
    std::string myloDir = parentDir(myloExecutable());
    if (!doc->path.empty() &&
        mylo_vm_options_find(found, sizeof(found), parentDir(doc->path).c_str(), myloDir.empty() ? nullptr : myloDir.c_str())) {
        char err[256];
        mylo_vm_options_load(&MyloVMOpts, found, err, sizeof(err));
    }

    auto& lsp = MyloLspProvider::instance();
    lsp.analyzeDocument(doc->editor->GetText(), doc->editor->GetCursorPosition().mLine, doc->path);
    MyloVMOpts = saved;

    TextEditor::ErrorMarkers markers;
    for (const auto& diagnostic : lsp.getDiagnostics()) {
        if (diagnostic.file.empty()) markers[diagnostic.line] = diagnostic.message;
    }
    doc->editor->SetErrorMarkers(markers);
    doc->diagnosticsDue = false;
    m_analyzedDocumentId = doc->id;
}

void IdeApp::updateWindowTitle() {
    IdeDocument* doc = activeDocument();
    std::string title = "Mylo IDE";
    if (doc) title += " - " + doc->name() + (doc->modified ? " *" : "");
    if (m_session.isDebugging()) title += m_session.isPaused() ? " [Paused]" : " [Debugging]";
    else if (m_session.isActive()) title += " [Running]";
    if (title != m_windowTitle) {
        m_windowTitle = title;
        SetWindowTitle(title.c_str());
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

void IdeApp::renderConsolePanel() {
    if (m_focusConsole) {
        ImGui::SetNextWindowFocus();
        m_focusConsole = false;
    }
    if (!ImGui::Begin("Console")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) m_session.clearConsole();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::SameLine();
    if (m_session.state() != DebugSession::State::Idle) ImGui::TextDisabled("%.2f s", m_session.elapsedSeconds());

    float inputHeight = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##ConsoleText", ImVec2(0, -inputHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImFont* mono = ImGui_ImplRaylib_GetMonoFont();
    ImGui::PushFont(mono, mono ? mono->LegacySize : 0.0f);   // the same size as the editor
    const auto& lines = m_session.console();
    ImGuiListClipper clipper;
    clipper.Begin((int)lines.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const ConsoleLine& line = lines[(size_t)i];
            ImGui::PushID(i);
            bool colored = line.kind != ConsoleLine::Kind::Out;
            if (line.kind == ConsoleLine::Kind::Err) ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            else if (line.kind == ConsoleLine::Kind::Info) ImGui::PushStyleColor(ImGuiCol_Text, kInfoColor);
            else if (line.kind == ConsoleLine::Kind::Input) ImGui::PushStyleColor(ImGuiCol_Text, kInputColor);
            std::string shown = line.kind == ConsoleLine::Kind::Input ? "> " + line.text : line.text;
            ImGui::TextUnformatted(shown.empty() ? " " : shown.c_str());
            if (colored) ImGui::PopStyleColor();

            // Lines naming a file and line, such as errors, open it when clicked.
            std::string path;
            int lineNo = 0;
            if (line.kind != ConsoleLine::Kind::Input && ImGui::IsItemHovered() && findLocation(line.text, path, lineNo)) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("Go to %s:%d", fileName(path).c_str(), lineNo);
                if (ImGui::IsMouseClicked(0)) goToLine(path, lineNo);
            }
            ImGui::PopID();
        }
    }
    ImGui::PopFont();
    if (m_session.consumeConsoleChanged() && m_autoScroll) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Input goes to the program's stdin when it's run without the debugger.
    bool canType = m_session.acceptsInput();
    ImGui::BeginDisabled(!canType);
    ImGui::SetNextItemWidth(-1.0f);
    const char* hint = canType ? "Input for the program, then Enter"
                               : (m_session.isDebugging() ? "Input isn't available while debugging; use Run instead"
                                                          : "Input for a running program");
    if (ImGui::InputTextWithHint("##Input", hint, m_inputLine, sizeof(m_inputLine), ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_session.sendInput(m_inputLine);
        m_inputLine[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::EndDisabled();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Debugger panels
// ---------------------------------------------------------------------------

void IdeApp::renderVariableRow(const VariableInfo& var, int depth) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID(&var);
    bool open = false;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns;
    if (var.reference > 0) {
        open = ImGui::TreeNodeEx(var.name.c_str(), flags);
    } else {
        ImGui::TreeNodeEx(var.name.c_str(), flags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
    }
    ImGui::TableNextColumn();
    if (var.type == "error") ImGui::TextColored(kErrorColor, "%s", var.value.c_str());
    else ImGui::TextUnformatted(var.value.c_str());
    if (ImGui::IsItemHovered() && var.value.size() > 40) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(var.value.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", var.type.c_str());
    if (open) {
        const std::vector<VariableInfo>* kids = m_session.children(var.reference);
        if (kids) renderVariableRows(*kids, depth + 1);
        else {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("loading...");
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void IdeApp::renderVariableRows(const std::vector<VariableInfo>& vars, int depth) {
    for (const auto& var : vars) renderVariableRow(var, depth);
}

void IdeApp::renderVariablesPanel() {
    if (!ImGui::Begin("Variables")) {
        ImGui::End();
        return;
    }
    if (!m_session.isPaused()) {
        ImGui::TextDisabled(m_session.isDebugging() ? "Pause the program to see its variables."
                                                    : "Start debugging (F5) and stop at a breakpoint to see variables.");
        ImGui::End();
        return;
    }
    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##Vars", 3, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.15f);
        ImGui::TableHeadersRow();

        struct Scope { const char* name; int ref; };
        Scope scopes[2] = {{"Locals", m_session.localsReference()}, {"Globals", m_session.globalsReference()}};
        for (const auto& scope : scopes) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool open = ImGui::TreeNodeEx(scope.name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns);
            if (open) {
                const std::vector<VariableInfo>* vars = m_session.children(scope.ref);
                if (!vars) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("loading...");
                } else if (vars->empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("(none)");
                } else {
                    renderVariableRows(*vars, 1);
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void IdeApp::renderWatchPanel() {
    if (!ImGui::Begin("Watch")) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4.0f);
    bool add = ImGui::InputTextWithHint("##Watch", "name, name.field or name[2]", m_watchInput, sizeof(m_watchInput),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    add |= ImGui::Button("Add");
    if (add && m_watchInput[0]) {
        m_session.watches().push_back(m_watchInput);
        m_watchInput[0] = '\0';
        m_session.refreshWatches();
    }

    auto& watches = m_session.watches();
    int removeIndex = -1;
    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (!watches.empty() && ImGui::BeginTable("##WatchTable", 3, flags)) {
        ImGui::TableSetupColumn("Expression", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.5f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)watches.size(); ++i) {
            ImGui::PushID(i);
            const VariableInfo* result = m_session.watchResult(watches[(size_t)i]);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool open = false;
            if (result && result->reference > 0) open = ImGui::TreeNodeEx(watches[(size_t)i].c_str(), ImGuiTreeNodeFlags_SpanAllColumns);
            else ImGui::TreeNodeEx(watches[(size_t)i].c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TableNextColumn();
            if (!m_session.isPaused()) ImGui::TextDisabled("-");
            else if (!result) ImGui::TextDisabled("...");
            else if (result->type == "error") ImGui::TextColored(kErrorColor, "%s", result->value.c_str());
            else ImGui::TextUnformatted(result->value.c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) removeIndex = i;
            if (open) {
                const std::vector<VariableInfo>* kids = m_session.children(result->reference);
                if (kids) {
                    for (const auto& kid : *kids) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TreeNodeEx(kid.name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(kid.value.c_str());
                        ImGui::TableNextColumn();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex >= 0) watches.erase(watches.begin() + removeIndex);
    if (watches.empty()) ImGui::TextDisabled("Watched values update each time the program pauses.");
    ImGui::End();
}

void IdeApp::renderCallStackPanel() {
    if (!ImGui::Begin("Call Stack")) {
        ImGui::End();
        return;
    }
    const auto& frames = m_session.frames();
    if (!m_session.isPaused() || frames.empty()) {
        ImGui::TextDisabled("The call stack appears when the program is paused.");
    } else {
        for (int i = 0; i < (int)frames.size(); ++i) {
            const auto& f = frames[(size_t)i];
            std::string label = f.name + "##frame" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), i == m_session.selectedFrame(), ImGuiSelectableFlags_AllowOverlap)) {
                m_session.selectFrame(i);
            }
            ImGui::SameLine(ImGui::GetFontSize() * 10.0f);
            ImGui::TextDisabled("%s:%d", fileName(f.path).c_str(), f.line);
        }
    }
    ImGui::End();
}

void IdeApp::renderBreakpointsPanel() {
    if (!ImGui::Begin("Breakpoints")) {
        ImGui::End();
        return;
    }
    auto all = allBreakpoints();
    if (all.empty()) {
        ImGui::TextDisabled("Click beside a line number, or press F9, to add a breakpoint.");
    } else {
        if (ImGui::Button("Remove All")) removeAllBreakpoints();
        const auto& verified = m_session.verifiedBreakpoints();
        std::string removePath;
        int removeLine = 0;
        for (const auto& entry : all) {
            for (int line : entry.second) {
                ImGui::PushID((entry.first + std::to_string(line)).c_str());
                if (ImGui::SmallButton("x")) { removePath = entry.first; removeLine = line; }
                ImGui::SameLine();
                std::string label = fileName(entry.first) + ":" + std::to_string(line);
                if (ImGui::Selectable(label.c_str())) goToLine(entry.first, line);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.first.c_str());
                // While debugging, say where the debugger actually put it.
                auto v = verified.find(entry.first);
                if (m_session.isDebugging() && v != verified.end() && !v->second.count(line)) {
                    ImGui::SameLine();
                    ImGui::TextColored(kWarnColor, "(moved or not hit)");
                }
                ImGui::PopID();
            }
        }
        if (!removePath.empty()) {
            if (IdeDocument* doc = findDocumentByPath(removePath)) {
                TextEditor::Breakpoints bps = doc->editor->GetBreakpoints();
                bps.erase(removeLine);
                doc->editor->SetBreakpoints(bps);
            }
            m_settings.breakpoints[removePath].erase(removeLine);
            m_session.setBreakpoints(removePath, allBreakpoints()[removePath]);
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Memory viewer
// ---------------------------------------------------------------------------

void IdeApp::renderMemoryPanel() {
    if (!ImGui::Begin("Memory")) {
        ImGui::End();
        return;
    }
    m_session.memoryWanted = true;
    if (!m_session.isPaused()) {
        ImGui::TextDisabled("VM memory appears when a debugged program is paused.");
        ImGui::End();
        return;
    }
    const Json& state = m_session.vmState();
    if (state.isNull()) {
        m_session.refreshMemory();
        ImGui::TextDisabled("Reading VM memory...");
        ImGui::End();
        return;
    }
    if (ImGui::Button("Refresh")) m_session.refreshMemory();
    ImGui::SameLine();
    ImGui::TextDisabled("ip %d   sp %d   fp %d   depth %d   line %d", state["ip"].asInt(), state["sp"].asInt(),
                        state["fp"].asInt(), state["depth"].asInt(), state["line"].asInt());

    if (ImGui::BeginTabBar("##MemoryTabs")) {
        if (ImGui::BeginTabItem("Overview", nullptr, m_selectMemoryTab == "Overview" ? ImGuiTabItemFlags_SetSelected : 0)) {
            usageBar("Stack", state["sp"].asInt() + 1, state["stackCapacity"].asInt(), "Raise stack_size in VM Options");
            usageBar("Strings", state["strCount"].asInt(), state["maxStrings"].asInt(), "Raise max_strings in VM Options");
            usageBar("Constants", state["constCount"].asInt(), state["maxConstants"].asInt(), "Raise max_constants in VM Options");
            usageBar("Bytecode", state["codeSize"].asInt(), state["maxCode"].asInt(), "Raise max_code in VM Options");
            ImGui::TextDisabled("Longest string: %s bytes.  Functions: %d.  Globals: %d.",
                                formatCount(state["maxStringLength"].asInt()).c_str(), state["functionCount"].asInt(),
                                state["globalCount"].asInt());
            ImGui::Separator();
            int heapMax = state["heapMax"].asInt();
            ImGui::Text("Arenas (heap_max: %s)", heapMax > 0 ? (formatCount(heapMax) + " slots").c_str() : "no limit");
            helpMarker("Arenas hold arrays, structs, maps and bytes. Arena 0 is the main heap; regions get their own.");
            ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg;
            if (ImGui::BeginTable("##Arenas", 4, flags)) {
                ImGui::TableSetupColumn("Arena", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
                ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0f);
                ImGui::TableHeadersRow();
                for (const auto& a : state["arenas"].items()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    int id = a["id"].asInt();
                    bool current = id == state["currentArena"].asInt();
                    ImGui::Text("%d%s", id, current ? " *" : "");
                    if (current && ImGui::IsItemHovered()) ImGui::SetTooltip("New allocations go here");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(a["active"].asBool() ? "active" : "freed");
                    ImGui::TableNextColumn();
                    int head = a["head"].asInt(), cap = a["capacity"].asInt();
                    std::string overlay = formatCount(head) + " / " + formatCount(cap);
                    ImGui::ProgressBar(cap > 0 ? (float)head / (float)cap : 0.0f, ImVec2(-1, 0), overlay.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", a["generation"].asInt());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stack", nullptr, m_selectMemoryTab == "Stack" ? ImGuiTabItemFlags_SetSelected : 0)) {
            const Json& stack = m_session.stackData();
            // Mark where each call frame begins: fp-2 holds the return address, fp-1 the caller's fp.
            std::map<int, std::string> frameAt;
            for (const auto& f : stack["frames"].items()) {
                if (f["fp"].asInt() >= 2) frameAt[f["fp"].asInt() - 2] = f["name"].asString("");
            }
            ImGui::TextDisabled("%d of %d slots in use", stack["total"].asInt(), stack["capacity"].asInt());
            ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("##StackTable", 4, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.5f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
                ImGui::TableHeadersRow();
                const auto& slots = stack["slots"].items();
                // Newest first, as the stack grows upward.
                for (auto it = slots.rbegin(); it != slots.rend(); ++it) {
                    const Json& s = *it;
                    int index = s["index"].asInt();
                    ImGui::TableNextRow();
                    auto frame = frameAt.find(index);
                    if (frame != frameAt.end()) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(80, 70, 30, 120));
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", index);
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", s["type"].asString("").c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(s["value"].asString("").c_str());
                    ImGui::TableNextColumn();
                    if (frame != frameAt.end()) ImGui::TextColored(kWarnColor, "%s: return ip", frame->second.c_str());
                    else if (frameAt.count(index - 1)) ImGui::TextDisabled("caller's fp");
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Heap", nullptr, m_selectMemoryTab == "Heap" ? ImGuiTabItemFlags_SetSelected : 0)) {
            const Json& arenas = state["arenas"];
            std::string current = "Arena " + std::to_string(m_memoryArena);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
            if (ImGui::BeginCombo("##Arena", current.c_str())) {
                for (const auto& a : arenas.items()) {
                    int id = a["id"].asInt();
                    std::string label = "Arena " + std::to_string(id) + (a["active"].asBool() ? "" : " (freed)");
                    if (ImGui::Selectable(label.c_str(), id == m_memoryArena)) {
                        m_memoryArena = id;
                        m_memoryArenaStart = 0;
                        m_session.requestArena(m_memoryArena, 0, kPageSize);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("<") && m_memoryArenaStart > 0) {
                m_memoryArenaStart = std::max(0, m_memoryArenaStart - kPageSize);
                m_session.requestArena(m_memoryArena, m_memoryArenaStart, kPageSize);
            }
            ImGui::SameLine();
            if (ImGui::Button(">")) {
                m_memoryArenaStart += kPageSize;
                m_session.requestArena(m_memoryArena, m_memoryArenaStart, kPageSize);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            int start = m_memoryArenaStart;
            if (ImGui::InputInt("Offset", &start, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_memoryArenaStart = std::max(0, start);
                m_session.requestArena(m_memoryArena, m_memoryArenaStart, kPageSize);
            }
            const Json& arena = m_session.arenaData();
            ImGui::SameLine();
            ImGui::TextDisabled("head %s of %s", formatCount(arena["head"].asInt()).c_str(), formatCount(arena["capacity"].asInt()).c_str());
            helpMarker("Each slot is a 64-bit value with a type tag. Objects start with a header: arrays hold "
                       "their type and length, structs their id and field count. Slots past the head are free.");

            ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("##HeapTable", 4, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
                ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 10.0f);
                ImGui::TableHeadersRow();
                int head = arena["head"].asInt();
                for (const auto& s : arena["slots"].items()) {
                    int offset = s["offset"].asInt();
                    bool free = offset >= head;
                    ImGui::TableNextRow();
                    if (free) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", offset);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", s["type"].asInt());
                    ImGui::TableNextColumn();
                    const Json& raw = s["raw"];
                    if (raw.isNumber()) {
                        double v = raw.asNumber();
                        if (v == (long long)v) ImGui::Text("%lld", (long long)v);
                        else ImGui::Text("%.10g", v);
                    } else {
                        ImGui::TextUnformatted("nan/inf");
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", s["bits"].asString("").c_str());
                    if (free) ImGui::PopStyleColor();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Strings", nullptr, m_selectMemoryTab == "Strings" ? ImGuiTabItemFlags_SetSelected : 0)) {
            const Json& strings = m_session.stringData();
            int total = strings["total"].asInt();
            ImGui::Text("%s of %s strings", formatCount(total).c_str(), formatCount(strings["max"].asInt()).c_str());
            helpMarker("Every distinct string the program makes, including each joined string, is kept here and "
                       "never freed. Long-running programs that build strings in a loop can fill it.");
            ImGui::SameLine();
            if (ImGui::Button("<##strings") && m_stringsStart > 0) {
                m_stringsStart = std::max(0, m_stringsStart - kPageSize);
                m_session.requestStrings(m_stringsStart, kPageSize);
            }
            ImGui::SameLine();
            if (ImGui::Button(">##strings") && m_stringsStart + kPageSize < total) {
                m_stringsStart += kPageSize;
                m_session.requestStrings(m_stringsStart, kPageSize);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##Filter", "Filter this page", m_stringFilter, sizeof(m_stringFilter));
            ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("##StringTable", 3, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
                ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
                ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const auto& s : strings["strings"].items()) {
                    const std::string& text = s["text"].asString();
                    if (m_stringFilter[0] && text.find(m_stringFilter) == std::string::npos) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", s["id"].asInt());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", s["length"].asInt());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(text.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Bytecode", nullptr, m_selectMemoryTab == "Bytecode" ? ImGuiTabItemFlags_SetSelected : 0)) {
            const Json& dis = m_session.disassembly();
            int ip = dis["ip"].asInt(-1);
            ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
            if (ImGui::BeginTable("##Code", 3, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
                ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.5f);
                ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const auto& ins : dis["instructions"].items()) {
                    int address = ins["address"].asInt();
                    ImGui::TableNextRow();
                    bool here = address == ip;
                    if (here) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(255, 214, 0, 60));
                    ImGui::TableNextColumn();
                    ImGui::Text("%s%d", here ? "> " : "  ", address);
                    if (here && m_scrollToIp) {
                        ImGui::SetScrollHereY(0.4f);
                        m_scrollToIp = false;
                    }
                    ImGui::TableNextColumn();
                    int line = ins["line"].asInt();
                    if (line > 0) ImGui::TextDisabled("%d", line);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(ins["text"].asString("").c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        m_selectMemoryTab.clear();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// VM options
// ---------------------------------------------------------------------------

std::string IdeApp::vmOptionsTargetPath() const {
    if (m_vmTarget == 1) {
        std::string myloDir = parentDir(myloExecutable());
        return myloDir.empty() ? "" : defaultConfigPath(myloDir);
    }
    const IdeDocument* doc = nullptr;
    for (const auto& d : m_documents) {
        if (d->id == m_activeDocumentId) doc = d.get();
    }
    if (!doc || doc->path.empty()) return "";
    return defaultConfigPath(parentDir(doc->path));
}

void IdeApp::loadVmOptions() {
    m_vmOptionsPath = vmOptionsTargetPath();
    mylo_vm_options_defaults(&m_vmOptions);
    m_vmFieldText.clear();
    m_vmFieldError.clear();
    m_vmMessage.clear();
    std::error_code ec;
    if (!m_vmOptionsPath.empty() && fs::exists(m_vmOptionsPath, ec)) {
        char err[512];
        if (!mylo_vm_options_load(&m_vmOptions, m_vmOptionsPath.c_str(), err, sizeof(err))) {
            m_vmMessage = std::string("This file has an error, so mylo won't run with it: ") + err;
        }
    }
    m_vmOptionsSaved = m_vmOptions;
}

void IdeApp::loadEffectiveOptions() {
    IdeDocument* doc = activeDocument();
    std::string script = doc ? doc->path : "";
    std::string myloDir = parentDir(myloExecutable());
    char found[MYLO_VM_PATH_LENGTH * 2];
    m_effectiveOptionsPath.clear();
    if (mylo_vm_options_find(found, sizeof(found), script.empty() ? nullptr : parentDir(script).c_str(),
                             myloDir.empty() ? nullptr : myloDir.c_str())) {
        m_effectiveOptionsPath = found;
    }
    m_effectiveOptionsScript = script;
}

void IdeApp::renderVmOptionsPanel() {
    if (!ImGui::Begin("VM Options")) {
        ImGui::End();
        return;
    }
    IdeDocument* doc = activeDocument();
    std::string script = doc ? doc->path : "";
    bool dirty = std::memcmp(&m_vmOptions, &m_vmOptionsSaved, sizeof(MyloVMOptions)) != 0;
    // Follow the active script, unless there are unsaved edits to keep.
    if ((script != m_vmOptionsScript || vmOptionsTargetPath() != m_vmOptionsPath) && !dirty) {
        m_vmOptionsScript = script;
        loadVmOptions();
        loadEffectiveOptions();
        dirty = false;
    }

    ImGui::TextWrapped("Limits for the Mylo VM, saved as mylovm.conf. They apply the next time you Run or Debug, "
                       "and when mylo runs the script from a terminal.");
    ImGui::Spacing();
    int target = m_vmTarget;
    ImGui::RadioButton("This script's folder", &target, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Global (beside mylo)", &target, 1);
    if (target != m_vmTarget) {
        if (dirty) m_vmMessage = "Save or revert your changes before switching files.";
        else m_vmTarget = target;
    }
    if (m_vmOptionsPath.empty()) {
        ImGui::TextColored(kWarnColor, m_vmTarget == 0 ? "Save the script first, so its folder is known." : "mylo wasn't found.");
        ImGui::End();
        return;
    }
    std::error_code ec;
    bool exists = fs::exists(m_vmOptionsPath, ec);
    ImGui::TextDisabled("%s%s", m_vmOptionsPath.c_str(), exists ? "" : " (not created yet)");

    if (!script.empty()) {
        if (m_effectiveOptionsScript != script) loadEffectiveOptions();
        if (m_effectiveOptionsPath.empty()) ImGui::TextDisabled("%s runs with the built-in defaults.", fileName(script).c_str());
        else if (samePath(m_effectiveOptionsPath, m_vmOptionsPath)) ImGui::TextColored(kGoodColor, "%s runs with these settings.", fileName(script).c_str());
        else ImGui::TextColored(kWarnColor, "%s runs with %s instead, which is found first.", fileName(script).c_str(), m_effectiveOptionsPath.c_str());
    }
    ImGui::Separator();

    MyloVMOptions defaults;
    mylo_vm_options_defaults(&defaults);
    int count = 0;
    const MyloOptionInfo* table = mylo_vm_option_table(&count);

    if (ImGui::BeginChild("##VmFields", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f))) {
        for (int i = 0; i < count; ++i) {
            const MyloOptionInfo& info = table[i];
            ImGui::PushID(info.key);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(info.key);
            helpMarker(info.description);
            float fieldX = ImGui::GetFontSize() * 10.0f;
            ImGui::SameLine(fieldX);

            if (info.kind == MYLO_OPT_INT) {
                std::string& text = m_vmFieldText[info.key];
                char current[64];
                mylo_vm_options_format(&m_vmOptions, &info, current, sizeof(current));
                // A rejected entry stays as typed until it's fixed. ImGui keeps its own copy of
                // the text while the field has focus, so this doesn't disturb typing.
                if (!m_vmFieldError.count(info.key)) text = current;
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
                // Leave room for the reset button in a narrow panel.
                ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 4.0f, std::min(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 4.5f)));
                if (ImGui::InputText("##value", buffer, sizeof(buffer))) {
                    text = buffer;
                    char err[256];
                    MyloVMOptions trial = m_vmOptions;
                    if (mylo_vm_options_set(&trial, info.key, buffer, err, sizeof(err))) {
                        m_vmOptions = trial;
                        m_vmFieldError.erase(info.key);
                    } else {
                        m_vmFieldError[info.key] = err;
                    }
                }
                char def[64];
                mylo_vm_options_format(&defaults, &info, def, sizeof(def));
                bool changed = mylo_vm_options_get_int(&m_vmOptions, &info) != info.default_value;
                if (changed) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("reset")) {
                        char err[64];
                        mylo_vm_options_set(&m_vmOptions, info.key, def, err, sizeof(err));
                        m_vmFieldError.erase(info.key);
                        m_vmFieldText.erase(info.key);
                    }
                }
            } else if (info.kind == MYLO_OPT_BOOL) {
                bool value = mylo_vm_options_get_int(&m_vmOptions, &info) != 0;
                if (ImGui::Checkbox("##value", &value)) {
                    char err[64];
                    mylo_vm_options_set(&m_vmOptions, info.key, value ? "true" : "false", err, sizeof(err));
                }
            } else {
                ImGui::NewLine();
                int removeIndex = -1;
                for (int p = 0; p < m_vmOptions.module_path_count; ++p) {
                    ImGui::PushID(p);
                    ImGui::Indent();
                    if (ImGui::SmallButton("x")) removeIndex = p;
                    ImGui::SameLine();
                    ImGui::TextUnformatted(m_vmOptions.module_paths[p]);
                    ImGui::Unindent();
                    ImGui::PopID();
                }
                if (removeIndex >= 0) {
                    for (int p = removeIndex; p + 1 < m_vmOptions.module_path_count; ++p)
                        std::memcpy(m_vmOptions.module_paths[p], m_vmOptions.module_paths[p + 1], sizeof(m_vmOptions.module_paths[p]));
                    std::memset(m_vmOptions.module_paths[m_vmOptions.module_path_count - 1], 0, sizeof(m_vmOptions.module_paths[0]));
                    m_vmOptions.module_path_count--;
                }
                ImGui::Indent();
                ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4.0f);
                bool add = ImGui::InputTextWithHint("##newpath", "folder to search for imports", m_vmNewPath, sizeof(m_vmNewPath),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                add |= ImGui::Button("Add");
                if (add && m_vmNewPath[0]) {
                    char err[256];
                    if (!mylo_vm_options_set(&m_vmOptions, "module_path", m_vmNewPath, err, sizeof(err))) m_vmMessage = err;
                    m_vmNewPath[0] = '\0';
                }
                ImGui::Unindent();
            }

            auto error = m_vmFieldError.find(info.key);
            if (error != m_vmFieldError.end()) {
                ImGui::SetCursorPosX(fieldX);
                ImGui::TextColored(kErrorColor, "%s", error->second.c_str());
            } else {
                ImGui::SetCursorPosX(fieldX);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushTextWrapPos(0.0f);
                if (info.kind == MYLO_OPT_INT) {
                    char def[64];
                    mylo_vm_options_format(&defaults, &info, def, sizeof(def));
                    ImGui::Text("%s Default: %s %s.", info.summary, def, info.unit);
                } else {
                    ImGui::TextUnformatted(info.summary);
                }
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (!m_vmMessage.empty()) ImGui::TextColored(kWarnColor, "%s", m_vmMessage.c_str());
    bool valid = m_vmFieldError.empty();
    ImGui::BeginDisabled(!dirty || !valid);
    if (ImGui::Button("Save")) {
        char err[512];
        if (mylo_vm_options_save(&m_vmOptions, m_vmOptionsPath.c_str(), err, sizeof(err))) {
            m_vmOptionsSaved = m_vmOptions;
            m_vmMessage.clear();
            m_status = "Saved " + m_vmOptionsPath;
            loadEffectiveOptions();
        } else {
            m_vmMessage = err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) loadVmOptions();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Defaults")) {
        mylo_vm_options_defaults(&m_vmOptions);
        m_vmFieldError.clear();
        m_vmFieldText.clear();
    }
    if (exists) {
        ImGui::SameLine();
        if (ImGui::Button("Delete File")) {
            fs::remove(m_vmOptionsPath, ec);
            loadVmOptions();
            loadEffectiveOptions();
            m_status = "Deleted " + m_vmOptionsPath;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scripts then use the next config mylo finds, or the defaults.");
    }
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(kWarnColor, "Unsaved");
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Popups
// ---------------------------------------------------------------------------

void IdeApp::beginBrowse(BrowseMode mode, int documentId) {
    m_browseMode = mode;
    m_browseDocumentId = documentId;
    m_openBrowsePopup = true;
    IdeDocument* doc = findDocument(documentId);
    IdeDocument* active = activeDocument();
    std::string base = (doc && !doc->path.empty()) ? doc->path : (active && !active->path.empty() ? active->path : "");
    std::error_code ec;
    m_browseDir = base.empty() ? fs::current_path(ec).string() : parentDir(base);
    std::snprintf(m_browseName, sizeof(m_browseName), "%s",
                  mode == BrowseMode::SaveAs ? ((doc && !doc->path.empty()) ? fileName(doc->path).c_str() : "untitled.mylo") : "");
    std::snprintf(m_browsePathInput, sizeof(m_browsePathInput), "%s", m_browseDir.c_str());
}

void IdeApp::renderBrowsePopup() {
    const char* title = m_browseMode == BrowseMode::SaveAs ? "Save Script As###Browse" : "Open Script###Browse";
    if (m_openBrowsePopup) {
        ImGui::OpenPopup("###Browse");
        m_openBrowsePopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 40.0f, ImGui::GetFontSize() * 28.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr)) return;

    std::error_code ec;
    if (ImGui::Button("Up")) {
        fs::path parent = fs::path(m_browseDir).parent_path();
        if (!parent.empty()) m_browseDir = parent.string();
        std::snprintf(m_browsePathInput, sizeof(m_browsePathInput), "%s", m_browseDir.c_str());
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##Dir", m_browsePathInput, sizeof(m_browsePathInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::string typed = m_browsePathInput;
        if (fs::is_directory(typed, ec)) m_browseDir = absolutePath(typed);
        else if (fs::is_regular_file(typed, ec) && m_browseMode == BrowseMode::Open) {
            openDocument(typed);
            ImGui::CloseCurrentPopup();
        }
    }

    std::string chosenFile;
    float footer = ImGui::GetFrameHeightWithSpacing() * 2.2f;
    if (ImGui::BeginChild("##Entries", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
        std::vector<fs::directory_entry> dirs, files;
        for (const auto& entry : fs::directory_iterator(m_browseDir, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code e2;
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (entry.is_directory(e2)) dirs.push_back(entry);
            else if (m_browseShowAll || entry.path().extension() == ".mylo" || entry.path().extension() == ".conf") files.push_back(entry);
        }
        auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.path().filename() < b.path().filename(); };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);
        for (const auto& d : dirs) {
            std::string label = "[" + d.path().filename().string() + "]";
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
                m_browseDir = d.path().string();
                std::snprintf(m_browsePathInput, sizeof(m_browsePathInput), "%s", m_browseDir.c_str());
            }
        }
        for (const auto& f : files) {
            std::string name = f.path().filename().string();
            bool selected = name == m_browseName;
            if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                std::snprintf(m_browseName, sizeof(m_browseName), "%s", name.c_str());
                if (ImGui::IsMouseDoubleClicked(0)) chosenFile = f.path().string();
            }
        }
        if (ec) ImGui::TextColored(kErrorColor, "Can't read this folder.");
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
    bool confirm = ImGui::InputText("File name", m_browseName, sizeof(m_browseName), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::Checkbox("All files", &m_browseShowAll);
    confirm |= ImGui::Button(m_browseMode == BrowseMode::SaveAs ? "Save" : "Open");
    ImGui::SameLine();
    bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (confirm && m_browseName[0]) chosenFile = (fs::path(m_browseDir) / m_browseName).string();

    if (!chosenFile.empty()) {
        ImGui::CloseCurrentPopup();
        if (m_browseMode == BrowseMode::Open) {
            openDocument(chosenFile);
        } else if (IdeDocument* doc = findDocument(m_browseDocumentId)) {
            if (fs::path(chosenFile).extension().empty()) chosenFile += ".mylo";
            doc->path = absolutePath(chosenFile);
            if (save(*doc)) {
                addRecent(doc->path);
                if (m_unsavedDocumentId == doc->id) finishUnsaved(doc->id);
                PendingAction after = m_afterSave;
                m_afterSave = PendingAction::None;
                if (after != PendingAction::None) {
                    m_activeDocumentId = doc->id;
                    start(after == PendingAction::Debug);
                }
            }
        }
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
        m_unsavedDocumentId = -1;
        m_quitting = false;
        m_afterSave = PendingAction::None;
    }
    ImGui::EndPopup();
}

void IdeApp::renderUnsavedPopup() {
    if (m_openUnsavedPopup) {
        ImGui::OpenPopup("Unsaved Changes");
        m_openUnsavedPopup = false;
    }
    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    IdeDocument* doc = findDocument(m_unsavedDocumentId);
    ImGui::Text("Save changes to %s?", doc ? doc->name().c_str() : "this script");
    if (ImGui::Button("Save") && doc) {
        ImGui::CloseCurrentPopup();
        if (save(*doc)) finishUnsaved(doc->id);
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save") && doc) {
        ImGui::CloseCurrentPopup();
        doc->modified = false;
        finishUnsaved(doc->id);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
        m_unsavedDocumentId = -1;
        m_quitting = false;
    }
    ImGui::EndPopup();
}

void IdeApp::renderSettingsPopup() {
    if (m_openSettingsPopup) {
        ImGui::OpenPopup("Settings");
        std::snprintf(m_settingsMyloPath, sizeof(m_settingsMyloPath), "%s", m_settings.myloPath.c_str());
        m_openSettingsPopup = false;
    }
    if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("mylo executable");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
    ImGui::InputTextWithHint("##mylo", myloExecutable().c_str(), m_settingsMyloPath, sizeof(m_settingsMyloPath));
    ImGui::TextDisabled("Leave empty to use the mylo beside the IDE, or the one on PATH.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::SliderFloat("Text size", &m_settings.fontSize, 10.0f, 40.0f, "%.0f");
    ImGui::Checkbox("Save all scripts before running", &m_settings.saveBeforeRun);
    ImGui::Spacing();
    if (ImGui::Button("OK")) {
        m_settings.myloPath = m_settingsMyloPath;
        std::string myloDir = parentDir(myloExecutable());
        if (!myloDir.empty()) std::snprintf(mylo_exe_dir, sizeof(mylo_exe_dir), "%s", myloDir.c_str());
        saveSettings();
        m_vmOptionsScript = "\x01";   // reload the VM options panel
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void IdeApp::renderAboutPopup() {
    if (m_openAboutPopup) {
        ImGui::OpenPopup("About Mylo IDE");
        m_openAboutPopup = false;
    }
    if (!ImGui::BeginPopupModal("About Mylo IDE", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Mylo IDE %s", MYLO_VERSION_FULL);
    ImGui::TextDisabled("A lightweight editor and debugger for the Mylo language.");
    ImGui::Spacing();
    ImGui::Text("mylo: %s", myloExecutable().c_str());
    ImGui::Text("Settings: %s", m_configDir.c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted("F5 debug or continue   Ctrl+F5 run   Shift+F5 stop");
    ImGui::TextUnformatted("F9 breakpoint   F10 step over   F11 step into   Shift+F11 step out");
    ImGui::Spacing();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
