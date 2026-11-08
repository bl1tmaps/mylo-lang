#pragma once

// Runs a script with `mylo`, either plainly (Run) or under the debug adapter (Debug), and
// keeps what the IDE shows about it: console output, the call stack, variables and VM memory.

#include "json.hpp"
#include "process.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct ConsoleLine {
    enum class Kind { Out, Err, Info, Input };
    Kind kind = Kind::Out;
    std::string text;
};

struct StackFrameInfo {
    int id = 0;
    std::string name;
    std::string path;
    int line = 0;
};

struct VariableInfo {
    std::string name;
    std::string value;
    std::string type;
    int reference = 0;   // > 0 when it has children to expand
    int childCount = 0;
};

struct LaunchOptions {
    std::string myloPath;      // the mylo executable
    std::string scriptPath;
    std::string configPath;    // a mylovm.conf to pass with --config, or empty
    std::string workingDir;
    bool stopOnEntry = false;
};

class DebugSession {
public:
    enum class State { Idle, Running, Paused, Exited };

    // Starts the script without the debugger; stdin is available for input.
    bool run(const LaunchOptions& options, std::string& error);
    // Starts the script under the debugger, with breakpoints given per file (1-based lines).
    bool debug(const LaunchOptions& options, const std::map<std::string, std::set<int>>& breakpoints, std::string& error);
    void stop();
    void update();

    State state() const { return m_state; }
    bool isActive() const { return m_state == State::Running || m_state == State::Paused; }
    bool isDebugging() const { return m_debugging && isActive(); }
    bool isPaused() const { return m_state == State::Paused; }
    bool acceptsInput() const { return !m_debugging && m_state == State::Running; }
    int exitCode() const { return m_exitCode; }
    double elapsedSeconds() const;
    const std::string& scriptPath() const { return m_options.scriptPath; }

    void continueRunning();
    void pause();
    void stepOver();
    void stepIn();
    void stepOut();
    void setBreakpoints(const std::string& path, const std::set<int>& lines);
    void sendInput(const std::string& line);

    // Console
    const std::vector<ConsoleLine>& console() const { return m_console; }
    void clearConsole() { m_console.clear(); }
    void addInfo(const std::string& text) { addLine(ConsoleLine::Kind::Info, text); }
    bool consumeConsoleChanged() { bool c = m_consoleChanged; m_consoleChanged = false; return c; }

    // Where the program is stopped
    const std::vector<StackFrameInfo>& frames() const { return m_frames; }
    int selectedFrame() const { return m_selectedFrame; }
    void selectFrame(int index);
    const std::string& stopReason() const { return m_stopReason; }
    const std::string& stopText() const { return m_stopText; }
    // True once after each stop, so the IDE can show the new location.
    bool consumeStopped() { bool s = m_justStopped; m_justStopped = false; return s; }
    // Verified breakpoint lines, as the debugger placed them.
    const std::map<std::string, std::set<int>>& verifiedBreakpoints() const { return m_verified; }

    // Variables. Children of an expandable value load when first asked for.
    int localsReference() const { return m_localsRef; }
    int globalsReference() const { return m_globalsRef; }
    const std::vector<VariableInfo>* children(int reference);   // nullptr while loading

    // Watch expressions, evaluated whenever the program stops.
    std::vector<std::string>& watches() { return m_watches; }
    const VariableInfo* watchResult(const std::string& expression) const;
    void refreshWatches();

    // VM memory, fetched on request while paused.
    void refreshMemory();
    void requestArena(int arena, int start, int count);
    void requestStrings(int start, int count);
    void requestStack(int start, int count);
    const Json& vmState() const { return m_vmState; }
    const Json& stackData() const { return m_stack; }
    const Json& arenaData() const { return m_arena; }
    const Json& stringData() const { return m_strings; }
    const Json& disassembly() const { return m_disassembly; }
    bool memoryWanted = false;   // set by the IDE while the memory panel is visible

    // Called with (file, line, message) when the script doesn't compile.
    std::function<void(const std::string&, int, const std::string&)> onCompileError;

private:
    using Callback = std::function<void(const Json&)>;

    bool startProcess(const std::vector<std::string>& args, std::string& error);
    void request(const std::string& command, Json arguments, Callback callback = nullptr);
    void handleMessage(const Json& message);
    void handleEvent(const Json& event);
    void onStopped(const Json& body);
    void loadFrames();
    void loadScopes();
    void finish(int exitCode);
    void addText(ConsoleLine::Kind kind, const std::string& text);
    void addLine(ConsoleLine::Kind kind, const std::string& text);
    void sendBreakpoints(const std::string& path, const std::set<int>& lines);
    void resume(const char* command);

    std::unique_ptr<ChildProcess> m_process;
    LaunchOptions m_options;
    State m_state = State::Idle;
    bool m_debugging = false;
    bool m_configured = false;
    int m_exitCode = -1;
    double m_startTime = 0.0;
    double m_endTime = 0.0;

    std::string m_protocolBuffer;
    int m_nextSeq = 1;
    std::map<int, Callback> m_pending;
    std::map<std::string, std::set<int>> m_initialBreakpoints;
    std::map<std::string, std::set<int>> m_verified;

    std::vector<ConsoleLine> m_console;
    std::string m_partial[2];   // unfinished last line of stdout and stderr
    bool m_consoleChanged = false;

    std::vector<StackFrameInfo> m_frames;
    int m_selectedFrame = 0;
    std::string m_stopReason;
    std::string m_stopText;
    bool m_justStopped = false;
    int m_stopGeneration = 0;   // replies from before the latest stop are stale

    int m_localsRef = 0;
    int m_globalsRef = 0;
    std::map<int, std::vector<VariableInfo>> m_children;
    std::set<int> m_loading;

    std::vector<std::string> m_watches;
    std::map<std::string, VariableInfo> m_watchResults;

    Json m_vmState, m_stack, m_arena, m_strings, m_disassembly;
};
