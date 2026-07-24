#include "project/ProcessLaunch.h"

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
