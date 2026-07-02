#include "project/ProcessLaunch.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

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

    const pid_t pid = fork();
    if (pid < 0)
        return fail("fork failed");

    if (pid == 0)
    {
        // Child failures are otherwise invisible, so report them with write(2).
        const auto childFail = [](const char* message) {
            (void)write(STDERR_FILENO, message, std::char_traits<char>::length(message));
            _exit(127);
        };

        if (!workingDir.empty() && chdir(workingDir.c_str()) != 0)
            childFail("[spawn:child] chdir to working directory failed\n");

        execv(executablePath.c_str(), childArgv.data());
        childFail("[spawn:child] execv failed: executable not found or not executable\n");
    }

    outPid = static_cast<long>(pid);
    return true;
#else
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
