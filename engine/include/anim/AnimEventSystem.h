#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimFlowState.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimSelectorState.h>
#include <ecs/Query.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class StoragePartitionSet;
class VerbDispatcher;
struct FixedLogicContext;

enum class AnimPendingKind : std::uint8_t
{
    // Content's clip event at index Event.
    Clip,
    // The lifecycle event of the behavior at index Behavior.
    BehaviorEntered,
    BehaviorExited,
    // The section lifecycle event of section Event of the flow at Content.
    SectionEntered,
    SectionExited,
};

// Names its event by value rather than pointing at a binding a reload could replace.
struct AnimPendingEvent
{
    AnimPendingKind Kind = AnimPendingKind::Clip;
    EntityId Producer;
    EntityId Instigator;
    DataAssetHandle Rig;
    // A record whose rig has rebound since is refused rather than re-resolved.
    std::uint64_t RigGeneration = 0;
    AnimTick Tick = 0;
    std::uint16_t Content = kAnimNoContent;
    std::uint16_t Event = 0;
    std::uint16_t Behavior = 0;
    std::uint8_t Layer = 0;
};

struct AnimEventGates
{
    // Gameplay events: only a World with simulation authority.
    bool Authority = true;
    // Cosmetic events: only a World that presents a pose.
    bool Presents = true;
};

// Appends what crossed at tick `now` to `pending`, up to `capacity`; a crossing
// past capacity is recorded as QueueFull. Shared with the preview.
void CollectAnimEvents(EntityId entity, DataAssetHandle rigHandle, const AnimBoundRig& rig,
                       const AnimSelectorState* selection, const AnimRequestSet* requests,
                       const AnimFlowState* flows, AnimContentState& content, AnimTick now, double tickSeconds,
                       AnimEventGates gates, std::vector<AnimPendingEvent>& pending, std::size_t capacity,
                       AnimDecisionLog* log);

// Runs on the owner thread. A null dispatcher answers Unavailable.
void DrainAnimEvents(World& world, std::span<const AnimPendingEvent> pending, VerbDispatcher* dispatcher);

class AnimEventSystem
{
public:
    AnimEventSystem(VerbDispatcher* dispatcher, bool presents);

    void FixedLogic(FixedLogicContext& ctx);
    // Collects and drains one tick outside a schedule: the preview and tests.
    void Run(World& world, AnimTick now, double tickSeconds);

    void SetCapacity(std::size_t capacity) { Capacity = capacity; }
    [[nodiscard]] std::size_t GetCapacity() const { return Capacity; }

private:
    void RunImpl(World& world, const StoragePartitionSet* partitions, AnimTick now, double tickSeconds);

    VerbDispatcher* Dispatcher = nullptr;
    bool Presents = true;
    std::size_t Capacity = 256;
    // Reused tick to tick, so a steady state allocates nothing.
    std::vector<AnimPendingEvent> Pending;
    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Write<AnimContentState>>> EventQuery;
};
