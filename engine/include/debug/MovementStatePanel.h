#pragma once

#include <debug/IDebugPanel.h>
#include <ecs/EntityId.h>
#include <ecs/Query.h>
#include <movement/MovementComponents.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

class World;
class DataAssetCache;

//=============================================================================
// MovementStatePanel
//
// Shows what movement actually resolved to for a character this frame: the
// facts it is standing on, which authored rules went live, the coefficients
// they produced, and whether the profile has reloaded since launch.
//
// This is the half of the tuning loop the editor cannot supply. The editor
// predicts what a profile will do; this reports what the running game did with
// it, so a designer can tell "my edit landed" from "my edit did nothing".
//
// Cost is confined to the overlay being open: Draw runs only then, and the
// trace-collecting resolve happens here for one selected entity. The fixed tick
// keeps resolving without a trace, unchanged.
//=============================================================================
class MovementStatePanel : public IDebugPanel
{
public:
    // The cache is optional: without it the panel still reports state, only
    // without reload counters.
    explicit MovementStatePanel(World& world, const DataAssetCache* dataAssets = nullptr);

    void Draw() override;

private:
    void DrawSelector();
    void DrawFacts(EntityId entity);
    void DrawReloadStatus();
    void DrawResolvedTuning(EntityId entity);

    World& WorldState;
    const DataAssetCache* DataAssets = nullptr;

    // Const access throughout: a diagnostic must never dirty change detection,
    // which is chunk-conservative and would mark whole columns as written.
    std::optional<Query<Read<CharacterMovement>>> Characters;
    const World* QueryWorld = nullptr;

    EntityId Selected;
    std::string LastBindError;

    // Sampled at draw rate, not tick rate: a feel readout, not a tick trace.
    std::array<float, 512> PlanarSpeed{};
    std::array<float, 512> VerticalSpeed{};
    std::size_t SampleCursor = 0;

    uint64_t LastReloadVersion = 0;
    uint32_t ReloadsSeen = 0;
};
