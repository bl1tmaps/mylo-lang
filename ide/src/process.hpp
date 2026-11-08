#pragma once

// Runs a program with its stdin, stdout and stderr connected to the IDE. Output is read on
// background threads and collected with takeOutput(), so the UI never waits on it.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ChildProcess {
public:
    enum class Stream { Out, Err };

    struct Chunk {
        Stream stream;
        std::string text;
    };

    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Starts args[0] with the remaining arguments, in the working directory cwd (empty for
    // the IDE's own). Returns false and sets error if it can't be started.
    bool start(const std::vector<std::string>& args, const std::string& cwd, std::string& error);

    bool isRunning();
    // The exit code once the program has ended, otherwise -1.
    int exitCode();

    bool write(const std::string& data);
    void closeInput();
    void kill();

    // Everything read since the last call, in order.
    std::vector<Chunk> takeOutput();
    // True once both output streams have closed and their text has been taken.
    bool outputFinished();

private:
    void reader(Stream stream);
    void reap();

    std::mutex m_mutex;
    std::vector<Chunk> m_pending;
    std::thread m_outThread;
    std::thread m_errThread;
    std::atomic<int> m_openStreams{0};
    bool m_started = false;
    bool m_exited = false;
    int m_exitCode = -1;

#ifdef _WIN32
    void* m_process = nullptr;
    void* m_stdin = nullptr;
    void* m_stdout = nullptr;
    void* m_stderr = nullptr;
#else
    int m_pid = -1;
    int m_stdin = -1;
    int m_stdout = -1;
    int m_stderr = -1;
#endif
};
