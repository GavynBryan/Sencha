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
// World is mutable for the authority's anchor on a flow's request and the tail that
// keeps a cancelled request while its flow plays out.
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
