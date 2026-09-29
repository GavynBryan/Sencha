#pragma once

#include <anim/AnimClock.h>
#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimPredicate.h>
#include <anim/AnimRequestReport.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimSelectorState.h>
#include <ecs/Query.h>

#include <optional>
#include <span>

class LoggingProvider;
class StoragePartitionSet;
struct FixedLogicContext;

// The request driving what a layer plays this tick, or null. Content time is
// measured from its start tick; docs/gameplay/animation.md gives the rules.
[[nodiscard]] const AnimRequest* AnimLayerDrivingRequest(const AnimBoundRig& rig, std::size_t layer,
                                                         const AnimSelectorState* selection,
                                                         const AnimRequestSet* requests, AnimTick now,
                                                         const AnimLayerContent* playing);

// What the selector's weight rules chose, or the rig's constant without a selector.
[[nodiscard]] float AnimLayerWeight(const AnimBoundRig& rig, std::size_t layer, const AnimSelectorState* selection);

[[nodiscard]] GameplayTagId AnimLayerBehavior(const AnimBoundRig& rig, std::size_t layer,
                                              const AnimSelectorState* selection,
                                              const AnimRequestSet* requests, AnimTick now,
                                              const AnimLayerContent* playing);

// The first merged slot row for `behavior` whose predicate passes, or -1.
[[nodiscard]] int ResolveAnimSlotRow(const AnimBoundRig& rig, GameplayTagId behavior,
                                     const AnimPredicateInputs& inputs);

// Resolves and advances every layer of one entity; shared with the preview. The
// World is mutable for what resolution writes back into the request set: the timing
// stamp, a flow's anchor, and the tail that keeps a cancelled request playing out.
void ResolveAnimEntity(World& world, EntityId entity, const AnimBoundRig& rig,
                       std::span<const std::uint32_t> facts, const AnimSelectorState* selection,
                       AnimContentState& content, AnimTick now, double tickSeconds, AnimDecisionLog* log);

// Runs after ResolveAnimEntity: counts requests that ended without a layer playing
// them and, on the authority, Held requests their producer ended without cancelling.
// True when it reported such an orphan this call.
bool NoteAnimRequestOutcomes(const World& world, EntityId entity, const AnimBoundRig& rig,
                             const AnimSelectorState* selection, const AnimContentState& content, AnimTick now,
                             AnimRequestReport& report, AnimDecisionLog* log);

class AnimContentSystem
{
public:
    explicit AnimContentSystem(bool presentsPose = true, LoggingProvider* logging = nullptr)
        : PresentsPose(presentsPose)
        , Logging(logging)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);
    void Resolve(World& world, AnimTick now, double tickSeconds);

private:
    void ResolveImpl(World& world, const StoragePartitionSet* partitions, const AnimClock& clock, double tickSeconds);

    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Write<AnimContentState>, Write<AnimRequestReport>>> ContentQuery;
    bool PresentsPose = true;
    LoggingProvider* Logging = nullptr;
};
