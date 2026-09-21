#include "project/ProcessLaunch.h"

#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <limits>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
    bool Wide(std::string_view text, std::wstring& out)
    {
        if (text.find('\0') != std::string_view::npos
            || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return false;
        if (text.empty()) { out.clear(); return true; }
        const auto size = static_cast<int>(text.size());
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
        if (length == 0) return false;
        out.resize(static_cast<std::size_t>(length));
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, out.data(), length) != 0;
    }

    // Windows passes one command-line string to the child's CRT. Escape both
    // literal quotes and trailing backslashes inside an always-quoted argument.
    void AppendArgument(std::wstring& line, std::wstring_view argument)
    {
        if (!line.empty()) line += L' ';
        line += L'"';
        std::size_t slashes = 0;
        for (const wchar_t c : argument)
        {
            if (c == L'\\') { ++slashes; continue; }
            line.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            line += c;
            slashes = 0;
        }
        line.append(slashes * 2, L'\\');
        line += L'"';
    }
#endif
#if defined(__unix__) || defined(__APPLE__)
    // What the child reports back through the close-on-exec pipe when it cannot
    // reach execv. On a successful exec the pipe's write end closes and the
    // parent reads end-of-file instead, which is how launch success is told
    // apart from a child that died before running.
    struct ChildFailure
    {
        std::int32_t Stage; // 1 = chdir, 2 = execv
        std::int32_t Errno;
    };
#endif
}

ChildProcess::~ChildProcess() { Close(); }

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : ProcessId(std::exchange(other.ProcessId, -1)), NativeHandle(std::exchange(other.NativeHandle, 0))
{
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept
{
    if (this != &other)
    {
        Close();
        ProcessId = std::exchange(other.ProcessId, -1);
        NativeHandle = std::exchange(other.NativeHandle, 0);
    }
    return *this;
}

void ChildProcess::Close()
{
#if defined(_WIN32)
    if (NativeHandle) CloseHandle(reinterpret_cast<HANDLE>(NativeHandle));
#endif
    NativeHandle = 0;
    ProcessId = -1;
}

bool ChildProcess::HasExited()
{
#if defined(_WIN32)
    if (NativeHandle == 0) return true;
    if (WaitForSingleObject(reinterpret_cast<HANDLE>(NativeHandle), 0) == WAIT_TIMEOUT)
        return false;
#else
    if (ProcessId < 0) return true;
    if (!HasProcessExited(ProcessId)) return false;
#endif
    Close();
    return true;
}

bool SpawnProcess(const std::string& executablePath,
                  const std::vector<std::string>& args,
                  const std::string& workingDir,
                  ChildProcess& outProcess,
                  std::string* error)
{
    ChildProcess child;
#if defined(_WIN32)
    const auto fail = [error](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    std::wstring executable;
    std::wstring directory;
    if (executablePath.empty() || !Wide(executablePath, executable) || !Wide(workingDir, directory))
        return fail("invalid UTF-8 process path or working directory");
    std::wstring commandLine;
    AppendArgument(commandLine, executable);
    for (const auto& arg : args)
    {
        std::wstring value;
        if (!Wide(arg, value)) return fail("invalid UTF-8 process argument");
        AppendArgument(commandLine, value);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, directory.empty() ? nullptr : directory.c_str(),
                        &startup, &info))
        return fail("CreateProcessW failed with error " + std::to_string(GetLastError()));
    CloseHandle(info.hThread);
    child.ProcessId = static_cast<long>(info.dwProcessId);
    child.NativeHandle = reinterpret_cast<std::uintptr_t>(info.hProcess);
#else
    if (!SpawnProcess(executablePath, args, workingDir, child.ProcessId, error))
        return false;
#endif
    outProcess = std::move(child);
    if (error) error->clear();
    return true;
}

bool SpawnProcess(const std::string& executablePath,
                  const std::vector<std::string>& args,
                  const std::string& workingDir,
                  long& outPid,
                  std::string* error)
{
    const auto fail = [error](std::string message) {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };

#if defined(__unix__) || defined(__APPLE__)
    // Built before fork: between fork and exec only async-signal-safe calls are
    // legal (the editors are multithreaded; another thread may hold the malloc
    // or stdio lock at fork time).
    std::vector<char*> childArgv;
    childArgv.reserve(args.size() + 2);
    childArgv.push_back(const_cast<char*>(executablePath.c_str()));
    for (const std::string& arg : args)
        childArgv.push_back(const_cast<char*>(arg.c_str()));
    childArgv.push_back(nullptr);

    // Close-on-exec pipe: a successful execv closes the write end and the parent
    // reads EOF; a failed execv (or chdir) leaves the child to write its failure
    // through before _exit, so the parent can report it instead of silently
    // handing back a pid for a process that never ran.
    int failPipe[2];
    if (pipe(failPipe) != 0)
        return fail(std::string("pipe failed: ") + std::strerror(errno));
    if (fcntl(failPipe[1], F_SETFD, FD_CLOEXEC) != 0)
    {
        const std::string message =
            std::string("fcntl(FD_CLOEXEC) failed: ") + std::strerror(errno);
        close(failPipe[0]);
        close(failPipe[1]);
        return fail(message);
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        const std::string message = std::string("fork failed: ") + std::strerror(errno);
        close(failPipe[0]);
        close(failPipe[1]);
        return fail(message);
    }

    if (pid == 0)
    {
        close(failPipe[0]);
        const auto childFail = [&](std::int32_t stage) {
            const ChildFailure record{ stage, static_cast<std::int32_t>(errno) };
            (void)write(failPipe[1], &record, sizeof(record));
            _exit(127);
        };

        if (!workingDir.empty() && chdir(workingDir.c_str()) != 0)
            childFail(1);

        execv(executablePath.c_str(), childArgv.data());
        childFail(2);
    }

    close(failPipe[1]);
    ChildFailure record{};
    const ssize_t got = read(failPipe[0], &record, sizeof(record));
    close(failPipe[0]);

    if (got == static_cast<ssize_t>(sizeof(record)))
    {
        // The child never ran; reap the zombie it left and report why.
        int status = 0;
        (void)waitpid(pid, &status, 0);
        const char* what = record.Stage == 1
            ? "failed to change directory to '"
            : "failed to execute '";
        const char* subject = record.Stage == 1 ? workingDir.c_str()
                                                 : executablePath.c_str();
        return fail(std::string(what) + subject + "': "
                    + std::strerror(record.Errno));
    }

    outPid = static_cast<long>(pid);
    return true;
#else
    (void)executablePath;
    (void)args;
    (void)workingDir;
    (void)outPid;
    return fail("process launch is only implemented on POSIX hosts");
#endif
}

bool HasProcessExited(long pid)
{
#if defined(__unix__) || defined(__APPLE__)
    if (pid <= 0)
        return true;
    const pid_t result = waitpid(static_cast<pid_t>(pid), nullptr, WNOHANG);
    if (result == 0)
        return false;
    // Reaped, or not our child (already reaped elsewhere): gone either way.
    return true;
#else
    (void)pid;
    return true;
#endif
}
