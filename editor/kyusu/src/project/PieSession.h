#pragma once

#include <string>
#include <vector>

// How a PIE child terminated. Crashed carries the signal number (e.g. SIGABRT
// from a failed assert); Exited carries the process exit code.
enum class PieExitKind
{
    None,
    Exited,
    Crashed,
};

struct PieExitStatus
{
    PieExitKind Kind = PieExitKind::None;
    int Value = 0;
};

//=============================================================================
// PieSession
//
// Out-of-process Play-In-Editor: launches the prebuilt `app` host against a
// project's game module and cooked content, and stops it. The runtime is a
// separate process (the same binary a shipped game runs), so a crash in play
// never takes the editor down, and PIE is literally the shipping path. One child
// at a time; Launch refuses while a session runs.
//=============================================================================
class PieSession
{
public:
    ~PieSession();

    // Spawns `app --game <gameModulePath> <startupArgs...>` with the working
    // directory set to workingDir (the project content root). startupArgs are
    // pre-tokenized +command arguments ("+map levels/x" or "+world w +zone id");
    // empty launches the host with no level. Returns false and sets *error if a
    // session is already running or the spawn fails.
    bool Launch(const std::string& appPath,
                const std::string& gameModulePath,
                const std::string& workingDir,
                const std::string& logPath,
                const std::vector<std::string>& startupArgs,
                std::string* error);

    void Stop();
    [[nodiscard]] bool IsRunning();

    // The child's termination detected since the last call, if any (one-shot):
    // IsRunning records it when it reaps a terminated child. Kind is None when
    // the child is still running or was stopped explicitly.
    [[nodiscard]] PieExitStatus TakeExit();

private:
    // pid_t kept as long to keep the platform header out of this interface; -1 is
    // "no child".
    long ChildPid = -1;
    PieExitStatus LastExit;
};
