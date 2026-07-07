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
                        const std::string& logPath,
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
    // The player mirrors its log into this file so the editor can tail it; the
    // player's stdout/stderr are the editor's own inherited streams, not capturable.
    if (!logPath.empty())
    {
        args.push_back("--log");
        args.push_back(logPath);
    }
    args.insert(args.end(), startupArgs.begin(), startupArgs.end());

    long pid = -1;
    if (!SpawnProcess(appPath, args, workingDir, pid, error))
        return false;

    ChildPid = pid;
    LastExit = {};
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
    int status = 0;
    const pid_t result = waitpid(static_cast<pid_t>(ChildPid), &status, WNOHANG);
    if (result == 0)
        return true; // still running
    // Reaped: record how it went (a crash vs a clean exit) before clearing, so
    // the driver can report it, then clear so a new session can start.
    if (result > 0)
    {
        if (WIFSIGNALED(status))
            LastExit = { PieExitKind::Crashed, WTERMSIG(status) };
        else if (WIFEXITED(status))
            LastExit = { PieExitKind::Exited, WEXITSTATUS(status) };
    }
    ChildPid = -1;
    return false;
#else
    return false;
#endif
}

PieExitStatus PieSession::TakeExit()
{
    const PieExitStatus exit = LastExit;
    LastExit = {};
    return exit;
}
