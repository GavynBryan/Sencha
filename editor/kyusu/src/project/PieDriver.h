#pragma once

#include "PieSession.h"

#include <string>
#include <vector>

class Engine;
class WorldDocument;
class ConsoleRegistry;
struct ProjectDescriptor;
struct RuntimeAssets;

// Launches and stops an out-of-process play session against the most recently
// published cook. Cooking is owned independently by CookSession.
class PieDriver
{
public:
    PieDriver(Engine& engine, WorldDocument& world, ProjectDescriptor* project, RuntimeAssets* assets);

    // Launches a PIE session for `map`; an empty map errors (cook a level first).
    void Play(const std::string& map);

    // The same session as a dedicated host plus a client joined to it: a
    // headless authority process and a windowed player process, which is the
    // topology a shipped session actually runs and the only way to see one
    // machine's view of another's simulation. Testing multiplayer through a
    // listen server hides everything that only a separate authority does, and
    // makes the host's rendering compete with the client's.
    void PlayHosted(const std::string& map);

    void Stop();
    [[nodiscard]] bool IsPlaying();
    [[nodiscard]] const std::string& LastCookedMap() const { return LastCookedMap_; }
    void UseCookedLevel(std::string map) {
        LastCookedMap_ = std::move(map);
        LastCookedWorld_.clear();
        LastCookedZone_.clear();
    }
    void UseCookedWorld(std::string world, std::string zone) {
        LastCookedMap_.clear();
        LastCookedWorld_ = std::move(world);
        LastCookedZone_ = std::move(zone);
    }

    // Registers play/stop/project. CookSession owns cook commands and settings.
    void RegisterCommands(ConsoleRegistry& registry);

private:
    // Resolves the prebuilt `app` host: beside the editor (installed SDK layout),
    // else build/app/app (the build tree, where editor and app sit in sibling dirs).
    [[nodiscard]] std::string ResolveHostAppPath() const;

    // What both play paths need before spawning anything: a project, a cooked
    // level to open, and the two binaries that have to exist for fork/exec to
    // report anything useful. Fills the startup arguments naming the level and
    // a label for the log. False means it already said why.
    [[nodiscard]] bool PreparePlay(const std::string& map,
                                   std::vector<std::string>& startupArgs,
                                   std::string& label,
                                   std::string& appPath);

    Engine&            Engine_;
    WorldDocument&     World_;
    ProjectDescriptor* Project_ = nullptr;
    RuntimeAssets*     Assets_ = nullptr;
    PieSession         Pie;
    // The authority process of a hosted session. Separate from Pie because the
    // two have different lifetimes to manage and different arguments; a session
    // stops client first so the host sees it leave.
    PieSession         HostPie;
    // Port a hosted session's authority binds. A collision surfaces as the host
    // process failing to start, through the same exit report as any other
    // launch failure.
    int                HostPort = 27500;
    // Last successfully cooked map ("levels/<name>"); `play` with no arg uses it,
    // closing the author -> cook -> play loop.
    std::string        LastCookedMap_;
    // World-mode cook result: the world file stem plus the focus zone's hex id.
    // Play launches `+world <stem> +zone <hex>` against the cooked world
    // manifest; the map fields and these are mutually exclusive.
    std::string        LastCookedWorld_;
    std::string        LastCookedZone_;
};
