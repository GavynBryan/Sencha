#include "PieSession.h"

#include "project/ProcessLaunch.h"

#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#endif

PieSession::~PieSession()
{
    Stop();
}

bool PieSession::Launch(const std::string& appPath,
                        const std::string& gameModulePath,
                        const std::string& workingDir,
                        const std::vector<std::string>& startupArgs,
                        std::string* error)
{
    if (IsRunning())
    {
        if (error != nullptr)
            *error = "a play session is already running";
        return false;
    }

    // Run in the project directory so the game's content roots ("assets",
    // "assets/.cooked") resolve relative to CWD, exactly as a shipped game does.
    std::vector<std::string> args{"--game", gameModulePath};
    args.insert(args.end(), startupArgs.begin(), startupArgs.end());

    long pid = -1;
    if (!SpawnProcess(appPath, args, workingDir, pid, error))
        return false;

    ChildPid = pid;
    return true;
}

void PieSession::Stop()
{
#if defined(__unix__) || defined(__APPLE__)
    if (ChildPid <= 0)
        return;
    kill(static_cast<pid_t>(ChildPid), SIGTERM);
    waitpid(static_cast<pid_t>(ChildPid), nullptr, 0);
#endif
    ChildPid = -1;
}

bool PieSession::IsRunning()
{
#if defined(__unix__) || defined(__APPLE__)
    if (ChildPid <= 0)
        return false;
    const pid_t result = waitpid(static_cast<pid_t>(ChildPid), nullptr, WNOHANG);
    if (result == 0)
        return true; // still running
    // Reaped (or vanished): clear so a new session can start.
    ChildPid = -1;
    return false;
#else
    return false;
#endif
}
