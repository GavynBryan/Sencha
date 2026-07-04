#pragma once

#include "PieSession.h"

#include <string>

class Engine;
class WorldDocument;
class ConsoleRegistry;
struct ProjectDescriptor;
struct RuntimeAssets;

// Drives the editor's author -> cook -> play loop: cooks the live document into
// the project's assets and launches/stops an out-of-process play session (PIE)
// bound to a cooked map. Owns the PieSession and registers the cook/play/stop/
// project console commands and the cook cell-size cvar. The toolbar Cook/Play/
// Stop buttons route here too.
class PieDriver
{
public:
    PieDriver(Engine& engine, WorldDocument& world, ProjectDescriptor* project, RuntimeAssets* assets);

    // Cooks the live document into the project's assets root, returning the cooked
    // map name ("levels/<name>") or empty on failure. An empty name defaults to the
    // document's file stem, else "untitled".
    std::string Cook(const std::string& levelName);
    // Launches a PIE session for `map`; an empty map errors (cook a level first).
    void Play(const std::string& map);
    void Stop();
    [[nodiscard]] bool IsPlaying();
    [[nodiscard]] const std::string& LastCookedMap() const { return LastCookedMap_; }

    // Registers `cook`/`play`/`stop`/`project` and the editor.cook.cell_size cvar.
    void RegisterCommands(ConsoleRegistry& registry);

private:
    // Resolves the prebuilt `app` host: beside the editor (installed SDK layout),
    // else build/app/app (the build tree, where editor and app sit in sibling dirs).
    [[nodiscard]] std::string ResolveHostAppPath() const;

    Engine&            Engine_;
    WorldDocument&     World_;
    ProjectDescriptor* Project_ = nullptr;
    RuntimeAssets*     Assets_ = nullptr;
    PieSession         Pie;
    // Last successfully cooked map ("levels/<name>"); `play` with no arg uses it,
    // closing the author -> cook -> play loop.
    std::string        LastCookedMap_;
    // World-mode cook result: the world file stem plus the focus zone's hex id.
    // Play launches `+world <stem> +zone <hex>` against the cooked world
    // manifest; the map fields and these are mutually exclusive.
    std::string        LastCookedWorld_;
    std::string        LastCookedZone_;
};
