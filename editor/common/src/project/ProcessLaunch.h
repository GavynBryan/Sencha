#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Owns observation of a launched child, not its lifetime: closing this handle
// never kills an editor. Windows keeps the native process handle so polling
// cannot accidentally observe a reused process id.
class ChildProcess
{
public:
    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    [[nodiscard]] long Pid() const { return ProcessId; }
    [[nodiscard]] bool HasExited();

private:
    friend bool SpawnProcess(const std::string&, const std::vector<std::string>&,
                             const std::string&, ChildProcess&, std::string*);
    void Close();
    long ProcessId = -1;
    std::uintptr_t NativeHandle = 0;
};

// Cross-platform launch with retained child identity. Paths and arguments are
// UTF-8; no shell interprets them. Existing out values survive a failed launch.
bool SpawnProcess(const std::string& executablePath,
                  const std::vector<std::string>& args,
                  const std::string& workingDir,
                  ChildProcess& outProcess,
                  std::string* error);

// Spawns a detached child process. args are the arguments after argv[0];
// workingDir, when non-empty, becomes the child's working directory. On
// success outPid holds the child's pid (as long, keeping platform process
// headers out of editor headers). The caller owns reaping (waitpid) if it
// wants exit status; an unreaped child is adopted by init at parent exit.
// POSIX-only; other hosts fail with an error.
bool SpawnProcess(const std::string& executablePath,
                  const std::vector<std::string>& args,
                  const std::string& workingDir,
                  long& outPid,
                  std::string* error);

// Non-blocking exit check for a pid this process spawned; reaps the child
// when it has exited. An unknown/already-reaped pid reports exited.
bool HasProcessExited(long pid);
