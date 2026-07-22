#pragma once

#include "PieSession.h"

#include <string>

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
