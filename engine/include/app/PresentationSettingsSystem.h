#pragma once

#include <render/GamePresentationLayer.h>
#include <render/PresentationSettings.h>
#include <render/PresentationSettingsPublisher.h>
#include <zone/ZoneId.h>

#include <optional>

class LoadedLevel;
class PostEffectCatalog;
class RuntimeWorld;
struct FrameUpdateContext;
struct RuntimeAssets;
struct WorldPartitionManifest;

// Resolves the presentation settings in effect at the play zone from the game's
// asset, the loaded world and the zone's scene. Registered only where a window
// presents. Docs: docs/renderer/presentation-settings.md.
class PresentationSettingsSystem
{
public:
    PresentationSettingsSystem(const LoadedLevel& level, const RuntimeWorld& runtime, RuntimeAssets& assets,
                               const PostEffectCatalog& effects);

    void FrameUpdate(FrameUpdateContext& ctx);

private:
    struct Inputs
    {
        const PresentationSettingsLayer* Game = nullptr;
        const WorldPartitionManifest* World = nullptr;
        ZoneId Zone;
        const PresentationSettingsLayer* Scene = nullptr;

        friend bool operator==(const Inputs&, const Inputs&) = default;
    };

    const LoadedLevel& Level;
    const RuntimeWorld& Runtime;
    GamePresentationLayer Game;
    PresentationSettingsPublisher Publisher;
    std::optional<Inputs> Resolved;
};
