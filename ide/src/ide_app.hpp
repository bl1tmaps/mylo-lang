#pragma once

// Mylo IDE: the editor, a run console, a debugger with breakpoints, variables and a memory
// viewer, and a panel for the VM settings in mylovm.conf.

#include "TextEditor.h"
#include "debug_session.hpp"

extern "C" {
#include "vm_options.h"
}

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct IdeDocument {
    int id = 0;
    std::string path;       // Empty until an untitled document is saved.
    std::unique_ptr<TextEditor> editor;
    std::string savedText;
    bool crlf = false;
    bool modified = false;
    bool diagnosticsDue = true;
    double lastEditTime = 0.0;

    std::string name() const;
};

struct IdeSettings {
    std::string myloPath;            // empty: the mylo beside the IDE, then PATH
    float fontSize = 18.0f;
    bool saveBeforeRun = true;
    std::vector<std::string> recentFiles;
    std::map<std::string, std::set<int>> breakpoints;
};

class IdeApp {
public:
    explicit IdeApp(std::string configDir);
    ~IdeApp();

    void openDocument(const std::string& path);
    void newDocument();
    bool isEmpty() const { return m_documents.empty(); }
    bool isDone() const { return m_done; }
    void requestQuit();

    // Called once per frame between ImGui::NewFrame and ImGui::Render.
    void update();

    const IdeSettings& settings() const { return m_settings; }
    const char* layoutPath() const { return m_layoutPath.c_str(); }

    // Automation, for tests and screenshots: one command per entry (see runAutomation).
    void setAutomation(std::vector<std::string> commands) { m_automation = std::move(commands); }
    // A screenshot the next frame should save, or "" for none. Clears the request.
    std::string takeScreenshotRequest() { std::string s; s.swap(m_screenshotPath); return s; }
    // True if an automation step timed out or wasn't understood.
    bool automationFailed() const { return m_automationFailed; }

private:
    enum class BrowseMode { None, Open, SaveAs };
    enum class PendingAction { None, Run, Debug };

    // Documents
    IdeDocument& addDocument(const std::string& path);
    IdeDocument* findDocument(int id);
    IdeDocument* findDocumentByPath(const std::string& path);
    IdeDocument* activeDocument() { return findDocument(m_activeDocumentId); }
    bool save(IdeDocument& doc);
    bool saveAll();
    void removeDocument(int id);
    void requestClose(int id);
    void continueQuit();
    void finishUnsaved(int id);
    void addRecent(const std::string& path);
    void goToLine(const std::string& path, int line);

    // Running and debugging
    void start(bool debug);
    void stopProgram();
    void restartProgram();
    std::string myloExecutable() const;
    std::map<std::string, std::set<int>> allBreakpoints() const;
    void syncBreakpoints();
    void toggleBreakpointAtCursor();
    void removeAllBreakpoints();
    void showStopLocation();
    void updateExecutionMarkers();

    // Layout
    void buildDefaultLayout(unsigned int dockspaceId);
    void renderMenuBar();
    void renderToolbar();
    void renderStatusBar();
    void handleShortcuts();

    // Panels
    void renderEditorPanel();
    void renderConsolePanel();
    void renderVariablesPanel();
    void renderWatchPanel();
    void renderCallStackPanel();
    void renderBreakpointsPanel();
    void renderMemoryPanel();
    void renderVmOptionsPanel();
    void renderVariableRows(const std::vector<VariableInfo>& vars, int depth);
    void renderVariableRow(const VariableInfo& var, int depth);

    // Popups
    void beginBrowse(BrowseMode mode, int documentId);
    void renderBrowsePopup();
    void renderUnsavedPopup();
    void renderSettingsPopup();
    void renderAboutPopup();

    // VM options
    std::string vmOptionsTargetPath() const;
    void loadVmOptions();
    void loadEffectiveOptions();

    void runAutomation();
    void updateDiagnostics();
    void updateWindowTitle();
    void loadSettings();
    void saveSettings();

    std::string m_configDir;
    std::string m_layoutPath;
    std::string m_settingsPath;
    IdeSettings m_settings;

    std::vector<std::unique_ptr<IdeDocument>> m_documents;
    int m_nextDocumentId = 1;
    int m_activeDocumentId = -1;
    int m_selectDocumentId = -1;
    int m_analyzedDocumentId = -1;

    int m_unsavedDocumentId = -1;
    bool m_openUnsavedPopup = false;
    bool m_quitting = false;
    bool m_done = false;

    BrowseMode m_browseMode = BrowseMode::None;
    int m_browseDocumentId = -1;
    bool m_openBrowsePopup = false;
    std::string m_browseDir;
    char m_browseName[512] = {};
    char m_browsePathInput[1024] = {};
    bool m_browseShowAll = false;
    PendingAction m_afterSave = PendingAction::None;

    bool m_openSettingsPopup = false;
    bool m_openAboutPopup = false;
    char m_settingsMyloPath[1024] = {};

    DebugSession m_session;
    std::string m_runScript;           // the script the last run started with
    bool m_lastRunWasDebug = false;
    bool m_focusConsole = false;
    bool m_focusEditor = false;
    char m_inputLine[1024] = {};
    bool m_autoScroll = true;
    char m_watchInput[256] = {};

    // Memory viewer
    int m_memoryArena = 0;
    int m_memoryArenaStart = 0;
    int m_stringsStart = 0;
    char m_stringFilter[128] = {};
    bool m_scrollToIp = false;
    std::string m_selectMemoryTab;     // a tab to bring forward, for automation

    // VM options panel
    int m_vmTarget = 0;                // 0 = beside the script, 1 = beside mylo (global)
    std::string m_vmOptionsPath;       // the file the panel shows
    std::string m_vmOptionsScript;     // the script it was loaded for
    MyloVMOptions m_vmOptions;
    MyloVMOptions m_vmOptionsSaved;
    std::map<std::string, std::string> m_vmFieldText;
    std::map<std::string, std::string> m_vmFieldError;
    std::string m_vmMessage;
    char m_vmNewPath[1024] = {};
    std::string m_effectiveOptionsPath;   // what the active script runs with
    std::string m_effectiveOptionsScript;

    std::string m_status;
    std::string m_windowTitle;
    bool m_layoutBuilt = false;

    std::vector<std::string> m_automation;
    size_t m_automationStep = 0;
    int m_automationWait = 0;
    double m_automationDeadline = 0.0;
    std::string m_screenshotPath;
    bool m_automationFailed = false;
};
