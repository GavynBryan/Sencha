#pragma once

#include <optional>
#include <string>
#include <vector>

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
                const std::vector<std::string>& startupArgs,
                std::string* error);

    void Stop();
    [[nodiscard]] bool IsRunning();

    // A one-shot description if the child ended abnormally (non-zero exit or a
    // signal other than the SIGTERM Stop sends) since the last call. Clean exits
    // and user-initiated stops report nothing. Poll this beside IsRunning so an
    // instant post-launch death (bad content, failed device init) surfaces
    // instead of the Play button just going dark.
    [[nodiscard]] std::optional<std::string> TakeExitReport();

private:
    void RecordExit(int status);

    // pid_t kept as long to keep the platform header out of this interface; -1 is
    // "no child".
    long ChildPid = -1;
    std::optional<std::string> PendingExitReport;
};
