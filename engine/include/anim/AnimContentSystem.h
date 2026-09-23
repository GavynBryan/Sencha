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
// one-shot and flow content is pinned at entry, and a flow then advances
// through its sections (AnimFlowRunner.h). A request superseding the one that
// drives pinned content starts it again when it resolves other content -- a
// combo's next swing -- and otherwise lets it run on. Content time advances
// every tick whether or not selection ran, and a layer publishes
// ContentComplete for the next tick's selection to read.
//
// A layer with no selector is request-keyed: the request driving it names the
// behavior, which is how a door plays Anim.Door.Open with no facts and no
// selector state. A layer with nothing selected or requested plays its idle
// behavior.
//=============================================================================

// The request driving what a layer plays this tick, or null.
//
// A request-keyed layer plays the newest live request claiming it (ties to the
// later sequence, so every machine picks the same one). Failing one, a flow
// the layer is `playing` and has not completed keeps the cancelled request
// that drives it for as long as that request is retained, unless its behavior
// aborts on cancel: that is how a flow plays a cancel out. A selector layer is driven by the
// request its latch holds, or by the primary record of the one intent its
// winning rule reads.
//
// Content time is measured from this request's start tick, which is what lets
// a late joiner reconstruct it.
[[nodiscard]] const AnimRequest* AnimLayerDrivingRequest(const AnimBoundRig& rig, std::size_t layer,
                                                         const AnimSelectorState* selection,
                                                         const AnimRequestSet* requests, AnimTick now,
                                                         const AnimLayerContent* playing);

// The behavior a layer plays this tick.
// The layer's weight this tick: what its selector's weight rules chose, or
// the rig's constant for a layer without a selector or before one runs.
[[nodiscard]] float AnimLayerWeight(const AnimBoundRig& rig, std::size_t layer, const AnimSelectorState* selection);

[[nodiscard]] GameplayTagId AnimLayerBehavior(const AnimBoundRig& rig, std::size_t layer,
                                              const AnimSelectorState* selection,
                                              const AnimRequestSet* requests, AnimTick now,
                                              const AnimLayerContent* playing);

// The first merged slot row for `behavior` whose predicate passes, or -1.
[[nodiscard]] int ResolveAnimSlotRow(const AnimBoundRig& rig, GameplayTagId behavior,
                                     const AnimPredicateInputs& inputs);

// Resolves and advances every layer of one entity. The pure half of the
// system, shared with the preview.
// Takes the World mutably for the one write resolution makes outside the
// layer's own state: the authority's anchor on a flow's request, and the tail
// that keeps a cancelled request while its flow plays out.
void ResolveAnimEntity(World& world, EntityId entity, const AnimBoundRig& rig,
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
