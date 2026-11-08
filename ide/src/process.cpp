#include "process.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

ChildProcess::~ChildProcess() {
    kill();
    closeInput();
    if (m_outThread.joinable()) m_outThread.join();
    if (m_errThread.joinable()) m_errThread.join();
#ifdef _WIN32
    if (m_stdout) CloseHandle((HANDLE)m_stdout);
    if (m_stderr) CloseHandle((HANDLE)m_stderr);
    if (m_process) CloseHandle((HANDLE)m_process);
#else
    if (m_stdout >= 0) close(m_stdout);
    if (m_stderr >= 0) close(m_stderr);
#endif
}

std::vector<ChildProcess::Chunk> ChildProcess::takeOutput() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Chunk> out;
    out.swap(m_pending);
    return out;
}

bool ChildProcess::outputFinished() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_openStreams.load() == 0 && m_pending.empty();
}

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// Quotes one argument so the program's C runtime splits it back out unchanged.
std::wstring quoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (size_t i = 0; ; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out += arg[i];
    }
    out += L'"';
    return out;
}

} // namespace

bool ChildProcess::start(const std::vector<std::string>& args, const std::string& cwd, std::string& error) {
    if (args.empty()) { error = "no program to run"; return false; }

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0) || !CreatePipe(&outRead, &outWrite, &sa, 0) || !CreatePipe(&errRead, &errWrite, &sa, 0)) {
        error = "could not create pipes";
        return false;
    }
    // The IDE's ends of the pipes mustn't be inherited, or the child would hold them open.
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring commandLine;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) commandLine += L' ';
        commandLine += quoteArgument(widen(args[i]));
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;

    PROCESS_INFORMATION pi = {};
    std::wstring wcwd = widen(cwd);
    BOOL ok = CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             nullptr, wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!ok) {
        DWORD code = GetLastError();
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(errRead);
        error = "could not start " + args[0] + " (error " + std::to_string((unsigned long)code) + ")";
        return false;
    }
    CloseHandle(pi.hThread);

    m_process = pi.hProcess;
    m_stdin = inWrite;
    m_stdout = outRead;
    m_stderr = errRead;
    m_started = true;
    m_openStreams = 2;
    m_outThread = std::thread(&ChildProcess::reader, this, Stream::Out);
    m_errThread = std::thread(&ChildProcess::reader, this, Stream::Err);
    return true;
}

void ChildProcess::reader(Stream stream) {
    HANDLE h = (HANDLE)(stream == Stream::Out ? m_stdout : m_stderr);
    char buffer[8192];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(h, buffer, sizeof(buffer), &n, nullptr) || n == 0) break;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pending.empty() && m_pending.back().stream == stream && m_pending.back().text.size() < 65536)
            m_pending.back().text.append(buffer, n);
        else
            m_pending.push_back({stream, std::string(buffer, n)});
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_openStreams--;
}

void ChildProcess::reap() {
    if (!m_started || m_exited || !m_process) return;
    if (WaitForSingleObject((HANDLE)m_process, 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess((HANDLE)m_process, &code);
        m_exitCode = (int)code;
        m_exited = true;
    }
}

bool ChildProcess::write(const std::string& data) {
    if (!m_stdin) return false;
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        DWORD n = 0;
        if (!WriteFile((HANDLE)m_stdin, p, (DWORD)left, &n, nullptr)) return false;
        p += n;
        left -= n;
    }
    return true;
}

void ChildProcess::closeInput() {
    if (m_stdin) {
        CloseHandle((HANDLE)m_stdin);
        m_stdin = nullptr;
    }
}

void ChildProcess::kill() {
    reap();
    if (m_started && !m_exited && m_process) {
        TerminateProcess((HANDLE)m_process, 1);
        WaitForSingleObject((HANDLE)m_process, 2000);
        reap();
    }
}

#else

bool ChildProcess::start(const std::vector<std::string>& args, const std::string& cwd, std::string& error) {
    if (args.empty()) { error = "no program to run"; return false; }
    int in[2], out[2], err[2];
    if (pipe(in) != 0 || pipe(out) != 0 || pipe(err) != 0) {
        error = std::string("could not create pipes: ") + std::strerror(errno);
        return false;
    }
    // A pipe that reports exec failure: it closes unread when exec succeeds.
    int status[2];
    if (pipe(status) != 0) { error = "could not create pipes"; return false; }
    fcntl(status[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        error = std::string("could not start: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(err[1], 2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]); close(err[0]); close(err[1]);
        close(status[0]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            int e = errno;
            (void)!::write(status[1], &e, sizeof(e));
            _exit(127);
        }
        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        int e = errno;
        (void)!::write(status[1], &e, sizeof(e));
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    close(err[1]);
    close(status[1]);
    int childErrno = 0;
    ssize_t got = read(status[0], &childErrno, sizeof(childErrno));
    close(status[0]);
    if (got == (ssize_t)sizeof(childErrno)) {
        close(in[1]); close(out[0]); close(err[0]);
        waitpid(pid, nullptr, 0);
        error = "could not start " + args[0] + ": " + std::strerror(childErrno);
        return false;
    }

    m_pid = pid;
    m_stdin = in[1];
    m_stdout = out[0];
    m_stderr = err[0];
    m_started = true;
    m_openStreams = 2;
    m_outThread = std::thread(&ChildProcess::reader, this, Stream::Out);
    m_errThread = std::thread(&ChildProcess::reader, this, Stream::Err);
    return true;
}

void ChildProcess::reader(Stream stream) {
    int fd = stream == Stream::Out ? m_stdout : m_stderr;
    char buffer[8192];
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pending.empty() && m_pending.back().stream == stream && m_pending.back().text.size() < 65536)
            m_pending.back().text.append(buffer, (size_t)n);
        else
            m_pending.push_back({stream, std::string(buffer, (size_t)n)});
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_openStreams--;
}

void ChildProcess::reap() {
    if (!m_started || m_exited || m_pid <= 0) return;
    int status = 0;
    pid_t r = waitpid(m_pid, &status, WNOHANG);
    if (r == m_pid) {
        m_exited = true;
        if (WIFEXITED(status)) m_exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) m_exitCode = 128 + WTERMSIG(status);
    }
}

bool ChildProcess::write(const std::string& data) {
    if (m_stdin < 0) return false;
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        ssize_t n = ::write(m_stdin, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        left -= (size_t)n;
    }
    return true;
}

void ChildProcess::closeInput() {
    if (m_stdin >= 0) {
        close(m_stdin);
        m_stdin = -1;
    }
}

void ChildProcess::kill() {
    reap();
    if (m_started && !m_exited && m_pid > 0) {
        ::kill(m_pid, SIGKILL);
        int status = 0;
        if (waitpid(m_pid, &status, 0) == m_pid) {
            m_exited = true;
            m_exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
    }
}

#endif

bool ChildProcess::isRunning() {
    reap();
    return m_started && !m_exited;
}

int ChildProcess::exitCode() {
    reap();
    return m_exited ? m_exitCode : -1;
}
