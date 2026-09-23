#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimPredicate.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimSelectorState.h>
#include <ecs/Query.h>

#include <optional>
#include <span>

class StoragePartitionSet;
struct FixedLogicContext;

//=============================================================================
// Content resolution
//
// After selection, every layer resolves its behavior to content through the
// rig's merged slot rows: first match, memoryless, every tick. Whether a new
// match applies depends on the behavior's kind -- cyclic and hold content
// switches rows mid-behavior with normalized time carried across, while
// one-shot and flow content is pinned at entry. Content time advances every
// tick whether or not selection ran, and a layer publishes ContentComplete for
// the next tick's selection to read.
//
// A layer with no selector is request-keyed: the newest live request claiming
// it names the behavior, which is how a door plays Anim.Door.Open with no
// facts and no selector state. A layer with nothing selected or requested
// plays its idle behavior.
//=============================================================================

// The request a request-keyed layer plays this tick: the newest live record
// claiming it, ties to the later sequence so every machine picks the same
// one. Null when none claims it.
[[nodiscard]] const AnimRequest* NewestAnimLayerRequest(const AnimRequestSet* requests, std::size_t layer,
                                                        AnimTick now);

// The behavior a layer plays this tick.
[[nodiscard]] GameplayTagId AnimLayerBehavior(const AnimBoundRig& rig, std::size_t layer,
                                              const AnimSelectorState* selection,
                                              const AnimRequestSet* requests, AnimTick now);

// The first merged slot row for `behavior` whose predicate passes, or -1.
[[nodiscard]] int ResolveAnimSlotRow(const AnimBoundRig& rig, GameplayTagId behavior,
                                     const AnimPredicateInputs& inputs);

// Resolves and advances every layer of one entity. The pure half of the
// system, shared with the preview.
void ResolveAnimEntity(const World& world, EntityId entity, const AnimBoundRig& rig,
                       std::span<const std::uint32_t> facts, const AnimSelectorState* selection,
                       AnimContentState& content, AnimTick now, double tickSeconds, AnimDecisionLog* log);

class AnimContentSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Resolve(World& world, AnimTick now, double tickSeconds);

private:
    void ResolveImpl(World& world, const StoragePartitionSet* partitions, AnimTick now, double tickSeconds);

    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Write<AnimContentState>>> ContentQuery;
};
