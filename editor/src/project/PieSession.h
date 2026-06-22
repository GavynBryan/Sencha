#pragma once

#include <string>

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

    // Spawns `app --game <gameModulePath> +map <map>` with the working directory
    // set to workingDir (the project content root). Returns false and sets *error
    // if a session is already running or the spawn fails. An empty map launches
    // the host with no level.
    bool Launch(const std::string& appPath,
                const std::string& gameModulePath,
                const std::string& workingDir,
                const std::string& map,
                std::string* error);

    void Stop();
    [[nodiscard]] bool IsRunning();

private:
    // pid_t kept as long to keep the platform header out of this interface; -1 is
    // "no child".
    long ChildPid = -1;
};
