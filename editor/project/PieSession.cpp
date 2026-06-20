#include "PieSession.h"

#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

PieSession::~PieSession()
{
    Stop();
}

bool PieSession::Launch(const std::string& appPath,
                        const std::string& gameModulePath,
                        const std::string& workingDir,
                        const std::string& map,
                        std::string* error)
{
    const auto fail = [error](std::string message) {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };

    if (IsRunning())
        return fail("a play session is already running");

#if defined(__unix__) || defined(__APPLE__)
    const pid_t pid = fork();
    if (pid < 0)
        return fail("fork failed");

    if (pid == 0)
    {
        // Child: run in the project directory so the game's content roots ("assets",
        // "assets/.cooked") resolve relative to CWD, exactly as a shipped game does.
        if (!workingDir.empty() && chdir(workingDir.c_str()) != 0)
            _exit(127);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(appPath.c_str()));
        argv.push_back(const_cast<char*>("--game"));
        argv.push_back(const_cast<char*>(gameModulePath.c_str()));
        std::string mapArg;
        if (!map.empty())
        {
            mapArg = "+map";
            argv.push_back(const_cast<char*>(mapArg.c_str()));
            argv.push_back(const_cast<char*>(map.c_str()));
        }
        argv.push_back(nullptr);

        execv(appPath.c_str(), argv.data());
        _exit(127); // execv only returns on failure
    }

    ChildPid = pid;
    return true;
#else
    return fail("PIE launch is only implemented on POSIX hosts");
#endif
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
