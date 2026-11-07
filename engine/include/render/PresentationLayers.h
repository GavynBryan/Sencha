#pragma once

#include <render/PresentationSettings.h>
#include <zone/ZoneId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

struct WorldPartitionManifest;

enum class PresentationLayerLevel : std::uint8_t
{
    Game,
    World,
    Graph,
    Scene,
};

struct PresentationLayerEntry
{
    PresentationLayerLevel Level = PresentationLayerLevel::Game;
    // The world's or graph's name; empty for the game and a scene.
    std::string_view Name;
    const PresentationSettingsLayer* Settings = nullptr;
};

class PresentationLayerStack
{
public:
    void Push(PresentationLayerEntry entry);
    [[nodiscard]] std::span<const PresentationLayerEntry> Layers() const { return { Entries.data(), Count }; }

private:
    std::array<PresentationLayerEntry, 4> Entries{};
    std::size_t Count = 0;
};

// The layers in effect in `zone`, lowest first, leaving out the ones that are
// absent. The entries point into `world` and the layers passed in.
[[nodiscard]] PresentationLayerStack PresentationLayersAt(const PresentationSettingsLayer* game,
                                                          const WorldPartitionManifest* world, ZoneId zone,
                                                          const PresentationSettingsLayer* scene);

[[nodiscard]] PresentationSettings ResolvePresentationSettings(std::span<const PresentationLayerEntry> layers);
