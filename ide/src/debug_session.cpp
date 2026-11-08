#include "debug_session.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

VariableInfo toVariable(const Json& v, const char* nameKey = "name", const char* valueKey = "value") {
    VariableInfo info;
    info.name = v[nameKey].asString("");
    info.value = v[valueKey].asString("");
    info.type = v["type"].asString("");
    info.reference = v["variablesReference"].asInt(0);
    info.childCount = v["indexedVariables"].asInt(0);
    return info;
}

} // namespace

double DebugSession::elapsedSeconds() const {
    if (m_state == State::Idle) return 0.0;
    return (m_state == State::Exited ? m_endTime : now()) - m_startTime;
}

bool DebugSession::startProcess(const std::vector<std::string>& args, std::string& error) {
    stop();
    m_process = std::make_unique<ChildProcess>();
    m_protocolBuffer.clear();
    m_pending.clear();
    m_partial[0].clear();
    m_partial[1].clear();
    m_frames.clear();
    m_children.clear();
    m_loading.clear();
    m_watchResults.clear();
    m_verified.clear();
    m_localsRef = m_globalsRef = 0;
    m_vmState = m_stack = m_arena = m_strings = m_disassembly = Json();
    m_stopReason.clear();
    m_stopText.clear();
    m_exitCode = -1;
    m_configured = false;
    if (!m_process->start(args, m_options.workingDir, error)) {
        m_process.reset();
        return false;
    }
    m_state = State::Running;
    m_startTime = now();
    return true;
}

bool DebugSession::run(const LaunchOptions& options, std::string& error) {
    m_options = options;
    m_debugging = false;
    std::vector<std::string> args = {options.myloPath, "--unbuffered"};
    if (!options.configPath.empty()) { args.push_back("--config"); args.push_back(options.configPath); }
    args.push_back(options.scriptPath);
    if (!startProcess(args, error)) return false;
    addInfo("Running " + options.scriptPath);
    return true;
}

bool DebugSession::debug(const LaunchOptions& options, const std::map<std::string, std::set<int>>& breakpoints, std::string& error) {
    m_options = options;
    m_debugging = true;
    m_initialBreakpoints = breakpoints;
    std::vector<std::string> args = {options.myloPath};
    if (!options.configPath.empty()) { args.push_back("--config"); args.push_back(options.configPath); }
    args.push_back("--dap");
    args.push_back(options.scriptPath);
    if (!startProcess(args, error)) return false;
    addInfo("Debugging " + options.scriptPath);

    Json init = Json::object();
    init["clientID"] = "mylo-ide";
    init["adapterID"] = "mylo";
    init["linesStartAt1"] = true;
    init["columnsStartAt1"] = true;
    request("initialize", init, [this](const Json&) {
        Json launch = Json::object();
        launch["program"] = m_options.scriptPath;
        launch["stopOnEntry"] = m_options.stopOnEntry;
        request("launch", launch);
    });
    return true;
}

void DebugSession::stop() {
    if (!m_process) return;
    if (m_debugging && isActive()) {
        // Ask politely first; the process is killed below either way.
        Json args = Json::object();
        request("disconnect", args);
    }
    m_process->kill();
    update();
    if (isActive()) finish(m_process ? m_process->exitCode() : -1);
}

void DebugSession::finish(int exitCode) {
    if (m_state == State::Exited || m_state == State::Idle) return;
    for (int i = 0; i < 2; ++i) {
        if (!m_partial[i].empty()) {
            addLine(i == 0 ? ConsoleLine::Kind::Out : ConsoleLine::Kind::Err, m_partial[i]);
            m_partial[i].clear();
        }
    }
    m_state = State::Exited;
    m_exitCode = exitCode;
    m_endTime = now();
    m_frames.clear();
    m_localsRef = m_globalsRef = 0;
    m_children.clear();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Exited with code %d after %.2f s", exitCode, m_endTime - m_startTime);
    addInfo(buf);
}

void DebugSession::addLine(ConsoleLine::Kind kind, const std::string& text) {
    m_console.push_back({kind, text});
    // Keep the console from growing without limit on a chatty program.
    if (m_console.size() > 20000) m_console.erase(m_console.begin(), m_console.begin() + 5000);
    m_consoleChanged = true;
}

void DebugSession::addText(ConsoleLine::Kind kind, const std::string& text) {
    std::string& partial = m_partial[kind == ConsoleLine::Kind::Err ? 1 : 0];
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        // Terminal colour codes (ESC [ ... letter) mean nothing in the console, so drop them.
        if (c == '\x1b') {
            if (i + 1 < text.size() && text[i + 1] == '[') {
                i += 2;
                while (i < text.size() && !((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z'))) ++i;
            }
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            addLine(kind, partial);
            partial.clear();
        } else {
            partial += c;
        }
    }
    m_consoleChanged = true;
}

void DebugSession::request(const std::string& command, Json arguments, Callback callback) {
    if (!m_process) return;
    int seq = m_nextSeq++;
    Json msg = Json::object();
    msg["seq"] = seq;
    msg["type"] = "request";
    msg["command"] = command;
    msg["arguments"] = std::move(arguments);
    std::string body = msg.dump();
    std::string framed = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    if (callback) m_pending[seq] = std::move(callback);
    m_process->write(framed);
}

void DebugSession::update() {
    if (!m_process) return;
    for (auto& chunk : m_process->takeOutput()) {
        if (chunk.stream == ChildProcess::Stream::Err || !m_debugging) {
            addText(chunk.stream == ChildProcess::Stream::Err ? ConsoleLine::Kind::Err : ConsoleLine::Kind::Out, chunk.text);
            continue;
        }
        m_protocolBuffer += chunk.text;
        for (;;) {
            size_t headerEnd = m_protocolBuffer.find("\r\n\r\n");
            if (headerEnd == std::string::npos) break;
            size_t lengthAt = m_protocolBuffer.find("Content-Length:");
            if (lengthAt == std::string::npos || lengthAt > headerEnd) {
                m_protocolBuffer.erase(0, headerEnd + 4);
                continue;
            }
            size_t length = (size_t)std::strtoul(m_protocolBuffer.c_str() + lengthAt + 15, nullptr, 10);
            if (m_protocolBuffer.size() < headerEnd + 4 + length) break;
            std::string body = m_protocolBuffer.substr(headerEnd + 4, length);
            m_protocolBuffer.erase(0, headerEnd + 4 + length);
            bool ok = false;
            Json message = Json::parse(body, &ok);
            if (ok) handleMessage(message);
        }
    }
    if (isActive() && !m_process->isRunning() && m_process->outputFinished()) {
        finish(m_process->exitCode());
    }
}

void DebugSession::handleMessage(const Json& message) {
    const std::string& type = message["type"].asString();
    if (type == "response") {
        int seq = message["request_seq"].asInt(-1);
        auto it = m_pending.find(seq);
        if (it != m_pending.end()) {
            Callback callback = std::move(it->second);
            m_pending.erase(it);
            if (callback) callback(message);
        }
    } else if (type == "event") {
        handleEvent(message);
    }
}

void DebugSession::handleEvent(const Json& event) {
    const std::string& name = event["event"].asString();
    const Json& body = event["body"];
    if (name == "output") {
        bool err = body["category"].asString("stdout") == "stderr";
        addText(err ? ConsoleLine::Kind::Err : ConsoleLine::Kind::Out, body["output"].asString(""));
    } else if (name == "initialized") {
        for (const auto& entry : m_initialBreakpoints) sendBreakpoints(entry.first, entry.second);
        request("configurationDone", Json::object(), [this](const Json&) { m_configured = true; });
    } else if (name == "stopped") {
        onStopped(body);
    } else if (name == "myloCompileError") {
        if (onCompileError) onCompileError(body["file"].asString(""), body["line"].asInt(0), body["message"].asString(""));
    } else if (name == "exited") {
        m_exitCode = body["exitCode"].asInt(0);
    } else if (name == "terminated") {
        int code = m_exitCode;
        Json args = Json::object();
        request("disconnect", args);
        finish(code < 0 ? 0 : code);
    }
}

void DebugSession::onStopped(const Json& body) {
    m_state = State::Paused;
    m_stopReason = body["reason"].asString("");
    m_stopText = body["text"].asString("");
    m_selectedFrame = 0;
    m_stopGeneration++;
    m_children.clear();
    m_loading.clear();
    m_localsRef = m_globalsRef = 0;
    if (m_stopReason == "exception" && !m_stopText.empty()) addInfo("Stopped on a runtime error: " + m_stopText);
    loadFrames();
    refreshWatches();
    // The memory panel fetches fresh data when it's next shown.
    m_vmState = Json();
}

void DebugSession::loadFrames() {
    int generation = m_stopGeneration;
    Json args = Json::object();
    args["threadId"] = 1;
    request("stackTrace", args, [this, generation](const Json& response) {
        if (generation != m_stopGeneration) return;
        m_frames.clear();
        for (const auto& f : response["body"]["stackFrames"].items()) {
            StackFrameInfo info;
            info.id = f["id"].asInt(0);
            info.name = f["name"].asString("");
            info.path = f["source"]["path"].asString("");
            info.line = f["line"].asInt(0);
            m_frames.push_back(info);
        }
        m_justStopped = true;
        loadScopes();
    });
}

void DebugSession::selectFrame(int index) {
    if (index < 0 || index >= (int)m_frames.size() || index == m_selectedFrame) return;
    m_selectedFrame = index;
    m_justStopped = true;   // so the editor shows that frame's line
    loadScopes();
    refreshWatches();
}

void DebugSession::loadScopes() {
    if (m_frames.empty()) return;
    int generation = m_stopGeneration;
    Json args = Json::object();
    args["frameId"] = m_frames[(size_t)m_selectedFrame].id;
    request("scopes", args, [this, generation](const Json& response) {
        if (generation != m_stopGeneration) return;
        const Json& scopes = response["body"]["scopes"];
        m_localsRef = scopes[0]["variablesReference"].asInt(0);
        m_globalsRef = scopes[1]["variablesReference"].asInt(0);
        children(m_localsRef);
        children(m_globalsRef);
    });
}

const std::vector<VariableInfo>* DebugSession::children(int reference) {
    if (reference <= 0) return nullptr;
    auto it = m_children.find(reference);
    if (it != m_children.end()) return &it->second;
    if (!isPaused() || m_loading.count(reference)) return nullptr;
    m_loading.insert(reference);
    int generation = m_stopGeneration;
    Json args = Json::object();
    args["variablesReference"] = reference;
    request("variables", args, [this, generation, reference](const Json& response) {
        if (generation != m_stopGeneration) return;
        m_loading.erase(reference);
        std::vector<VariableInfo>& list = m_children[reference];
        list.clear();
        for (const auto& v : response["body"]["variables"].items()) list.push_back(toVariable(v));
    });
    return nullptr;
}

const VariableInfo* DebugSession::watchResult(const std::string& expression) const {
    auto it = m_watchResults.find(expression);
    return it == m_watchResults.end() ? nullptr : &it->second;
}

void DebugSession::refreshWatches() {
    m_watchResults.clear();
    if (!isPaused()) return;
    int generation = m_stopGeneration;
    for (const auto& expression : m_watches) {
        if (expression.empty()) continue;
        Json args = Json::object();
        args["expression"] = expression;
        args["context"] = "watch";
        if (!m_frames.empty()) args["frameId"] = m_frames[(size_t)m_selectedFrame].id;
        else args["frameId"] = 1;
        request("evaluate", args, [this, generation, expression](const Json& response) {
            if (generation != m_stopGeneration) return;
            VariableInfo info;
            info.name = expression;
            if (response["success"].asBool(false)) {
                info = toVariable(response["body"], "name", "result");
                info.name = expression;
            } else {
                info.value = response["message"].asString("not available");
                info.type = "error";
            }
            m_watchResults[expression] = info;
        });
    }
}

void DebugSession::refreshMemory() {
    if (!isPaused()) return;
    int generation = m_stopGeneration;
    request("myloVmState", Json::object(), [this, generation](const Json& r) {
        if (generation == m_stopGeneration && r["success"].asBool(false)) m_vmState = r["body"];
    });
    Json dis = Json::object();
    dis["count"] = 96;
    request("myloDisassemble", dis, [this, generation](const Json& r) {
        if (generation == m_stopGeneration && r["success"].asBool(false)) m_disassembly = r["body"];
    });
    requestStack(-512, 512);   // the newest 512 slots
    requestStrings(m_strings["start"].asInt(0), 256);
    requestArena(m_arena.isNull() ? 0 : m_arena["arena"].asInt(0), m_arena["start"].asInt(0), 256);
}

void DebugSession::requestStack(int start, int count) {
    if (!isPaused()) return;
    Json args = Json::object();
    args["start"] = start;
    args["count"] = count;
    request("myloStack", args, [this](const Json& r) {
        if (r["success"].asBool(false)) m_stack = r["body"];
    });
}

void DebugSession::requestStrings(int start, int count) {
    if (!isPaused()) return;
    Json args = Json::object();
    args["start"] = start;
    args["count"] = count;
    request("myloStrings", args, [this, start](const Json& r) {
        if (!r["success"].asBool(false)) return;
        m_strings = r["body"];
        m_strings["start"] = start;
    });
}

void DebugSession::requestArena(int arena, int start, int count) {
    if (!isPaused()) return;
    Json args = Json::object();
    args["arena"] = arena;
    args["start"] = start;
    args["count"] = count;
    request("myloArena", args, [this, start](const Json& r) {
        if (!r["success"].asBool(false)) return;
        m_arena = r["body"];
        m_arena["start"] = start;
    });
}

void DebugSession::resume(const char* command) {
    if (!isPaused()) return;
    m_state = State::Running;
    m_frames.clear();
    m_children.clear();
    m_localsRef = m_globalsRef = 0;
    m_stopGeneration++;
    Json args = Json::object();
    args["threadId"] = 1;
    request(command, args);
}

void DebugSession::continueRunning() { resume("continue"); }
void DebugSession::stepOver() { resume("next"); }
void DebugSession::stepIn() { resume("stepIn"); }
void DebugSession::stepOut() { resume("stepOut"); }

void DebugSession::pause() {
    if (!m_debugging || m_state != State::Running) return;
    Json args = Json::object();
    args["threadId"] = 1;
    request("pause", args);
}

void DebugSession::sendBreakpoints(const std::string& path, const std::set<int>& lines) {
    Json args = Json::object();
    Json source = Json::object();
    source["path"] = path;
    args["source"] = source;
    Json list = Json::array();
    for (int line : lines) {
        Json bp = Json::object();
        bp["line"] = line;
        list.push(bp);
    }
    args["breakpoints"] = list;
    request("setBreakpoints", args, [this, path](const Json& r) {
        std::set<int>& verified = m_verified[path];
        verified.clear();
        for (const auto& bp : r["body"]["breakpoints"].items()) {
            if (bp["verified"].asBool(false)) verified.insert(bp["line"].asInt(0));
        }
    });
}

void DebugSession::setBreakpoints(const std::string& path, const std::set<int>& lines) {
    m_initialBreakpoints[path] = lines;
    if (m_debugging && isActive() && m_configured) sendBreakpoints(path, lines);
}

void DebugSession::sendInput(const std::string& line) {
    if (!acceptsInput()) return;
    // A prompt is usually waiting on an unfinished line; show the answer after it.
    if (!m_partial[0].empty()) {
        addLine(ConsoleLine::Kind::Out, m_partial[0] + line);
        m_partial[0].clear();
    } else {
        addLine(ConsoleLine::Kind::Input, line);
    }
    m_process->write(line + "\n");
}
