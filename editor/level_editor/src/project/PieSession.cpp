#include "PieSession.h"

#include "project/ProcessLaunch.h"

#include <string>
#include <thread>
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

void PieSession::Stop(std::chrono::milliseconds grace)
{
#if defined(__unix__) || defined(__APPLE__)
    if (ChildPid <= 0)
        return;
    const auto child = static_cast<pid_t>(ChildPid);
    kill(child, SIGTERM);

    const auto deadline = std::chrono::steady_clock::now() + grace;
    int status = 0;
    pid_t reaped = waitpid(child, &status, WNOHANG);
    while (reaped == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        reaped = waitpid(child, &status, WNOHANG);
    }

    if (reaped == 0)
    {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        PendingExitReport = "runtime did not stop within "
            + std::to_string(grace.count()) + " ms and was killed";
    }
    else if (reaped > 0 && !(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM))
    {
        RecordExit(status);
    }
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
    if (result > 0)
        RecordExit(status);
    // Reaped (or vanished): clear so a new session can start.
    ChildPid = -1;
    return false;
#else
    return false;
#endif
}

void PieSession::RecordExit(int status)
{
#if defined(__unix__) || defined(__APPLE__)
    if (WIFEXITED(status))
    {
        const int code = WEXITSTATUS(status);
        if (code != 0)
            PendingExitReport = "runtime exited with code " + std::to_string(code)
                + " (127 is a failed launch: missing binary or module)";
    }
    else if (WIFSIGNALED(status))
    {
        PendingExitReport =
            "runtime terminated by signal " + std::to_string(WTERMSIG(status));
    }
#else
    (void)status;
#endif
}

std::optional<std::string> PieSession::TakeExitReport()
{
    std::optional<std::string> report = std::move(PendingExitReport);
    PendingExitReport.reset();
    return report;
}
