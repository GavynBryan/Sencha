#pragma once

#include <anim/AnimClock.h>
#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimEventCursor.h>
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

class LoggingProvider;
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
    // The invocation that asked for the request behind it, when one did.
    InvocationId Cause;
    DataAssetHandle Rig;
    // A record whose rig has rebound since is refused rather than re-resolved.
    std::uint64_t RigGeneration = 0;
    AnimTick Tick = 0;
    std::uint16_t Content = kAnimNoContent;
    std::uint16_t Event = 0;
    std::uint16_t Behavior = 0;
    std::uint8_t Layer = 0;
};

// One tick's crossings waiting for the owner thread. Gameplay and cosmetic events
// queue apart, each bounded by its own capacity, so cosmetic traffic can never take a
// gameplay event's place. A crossing past its queue's capacity is refused and counted.
struct AnimPendingEvents
{
    std::vector<AnimPendingEvent> Gameplay;
    std::vector<AnimPendingEvent> Cosmetic;
    std::size_t GameplayCapacity = 256;
    std::size_t CosmeticCapacity = 256;
    std::uint32_t GameplayRefused = 0;
    std::uint32_t CosmeticRefused = 0;

    [[nodiscard]] std::vector<AnimPendingEvent>& Queue(AnimEventScope scope)
    {
        return scope == AnimEventScope::Gameplay ? Gameplay : Cosmetic;
    }
    // False, and counted, when the scope's queue is full.
    [[nodiscard]] bool Admit(AnimEventScope scope);
    void Clear();
};

struct AnimEventGates
{
    // Gameplay events: only a World with simulation authority.
    bool Authority = true;
    // Cosmetic events: only a World that presents a pose.
    bool Presents = true;
};

// Queues what crossed at tick `now`; a crossing its queue refuses is recorded as
// QueueFull. Shared with the preview.
void CollectAnimEvents(EntityId entity, DataAssetHandle rigHandle, const AnimBoundRig& rig,
                       const AnimSelectorState* selection, const AnimRequestSet* requests,
                       const AnimFlowState* flows, const AnimContentState& content, AnimEventCursor& cursor,
                       AnimTick now, double tickSeconds, AnimEventGates gates, AnimPendingEvents& pending,
                       AnimDecisionLog* log);

// Runs on the owner thread, gameplay events first. A null dispatcher answers Unavailable.
void DrainAnimEvents(World& world, const AnimPendingEvents& pending, VerbDispatcher* dispatcher);

class AnimEventSystem
{
public:
    AnimEventSystem(VerbDispatcher* dispatcher, bool presents, LoggingProvider* logging = nullptr);

    void FixedLogic(FixedLogicContext& ctx);
    // Collects and drains one tick outside a schedule: the preview and tests.
    void Run(World& world, AnimTick now, double tickSeconds);

    void SetCapacity(AnimEventScope scope, std::size_t capacity);
    [[nodiscard]] std::size_t GetCapacity(AnimEventScope scope) const;
    // Crossings refused since the system started, by scope.
    [[nodiscard]] std::uint64_t RefusedGameplay() const { return TotalGameplayRefused; }
    [[nodiscard]] std::uint64_t RefusedCosmetic() const { return TotalCosmeticRefused; }

private:
    void RunImpl(World& world, const StoragePartitionSet* partitions, const AnimClock& clock, double tickSeconds);

    VerbDispatcher* Dispatcher = nullptr;
    bool Presents = true;
    LoggingProvider* Logging = nullptr;
    // Reused tick to tick, so a steady state allocates nothing.
    AnimPendingEvents Pending;
    std::uint64_t TotalGameplayRefused = 0;
    std::uint64_t TotalCosmeticRefused = 0;
    // Whether the last tick refused a gameplay event, so an overflow is reported as it
    // starts rather than every tick it lasts.
    bool Overflowing = false;
    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Read<AnimContentState>, Write<AnimEventCursor>>> EventQuery;
};
