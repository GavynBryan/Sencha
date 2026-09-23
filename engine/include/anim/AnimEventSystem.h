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

//=============================================================================
// Clip events
//
// After content resolution, each layer's content time has advanced by one
// tick, and every event mark inside the stretch it advanced over is crossed.
// Crossing is once per content instance per mark (per loop, for cyclic
// content), measured on the tick clock rather than the frame clock, so every
// machine that plays the same content crosses the same marks on the same
// ticks. A layer whose behavior changed first leaves the old behavior and
// enters the new one, firing whichever of their lifecycle events is declared,
// and a flow that changed section does the same for its sections.
//
// Collection and dispatch are separate passes. Collection reads content state
// and appends fixed-size pending records that name an event by rig, content
// and index; it never calls a verb, which is what lets it move to a job
// later. The drain runs on the owner thread, resolves each record's binding
// by key in the rig's current binding set, and offers it through the
// dispatcher. Every crossing is recorded in the entity's decision log --
// fired with its admission, skipped, or below weight -- so an event that did
// nothing says why.
//
// Scope gates production, not authority. A gameplay event is produced only in
// a World with simulation authority; a cosmetic one only where a pose is
// presented. Neither makes the invocation authoritative: the verb decides.
//=============================================================================

// Which kind of event a pending record names.
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

// One crossing, waiting for the drain. Value-only: it names the event rather
// than pointing at a binding a reload could replace before the drain runs.
struct AnimPendingEvent
{
    AnimPendingKind Kind = AnimPendingKind::Clip;
    EntityId Producer;
    EntityId Instigator;
    DataAssetHandle Rig;
    // The binding generation the event was read from. A record whose rig has
    // rebound since is stale and is refused rather than re-resolved.
    std::uint64_t RigGeneration = 0;
    AnimTick Tick = 0;
    std::uint16_t Content = kAnimNoContent;
    std::uint16_t Event = 0;
    std::uint16_t Behavior = 0;
    std::uint8_t Layer = 0;
};

// What this World may produce.
struct AnimEventGates
{
    // Gameplay events: only a World with simulation authority.
    bool Authority = true;
    // Cosmetic events: only a World that presents a pose.
    bool Presents = true;
};

// Crosses one entity's events for tick `now` and appends what fired to
// `pending`, up to `capacity`. A crossing past capacity is recorded as
// QueueFull rather than dropped unseen. The pure half of the system, shared
// with the preview.
void CollectAnimEvents(EntityId entity, DataAssetHandle rigHandle, const AnimBoundRig& rig,
                       const AnimSelectorState* selection, const AnimRequestSet* requests,
                       const AnimFlowState* flows, AnimContentState& content, AnimTick now, double tickSeconds,
                       AnimEventGates gates, std::vector<AnimPendingEvent>& pending, std::size_t capacity,
                       AnimDecisionLog* log);

// Offers each pending event to its binding, in order, on the owner thread,
// and records the admission. A null dispatcher answers Unavailable: the
// events were produced, and nothing here can run them.
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
