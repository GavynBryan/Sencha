#pragma once

#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimRigComposition.h>
#include <ecs/Query.h>

#include <cstdint>
#include <optional>
#include <unordered_set>

class CommandBuffer;
class LoggingProvider;
class StoragePartitionSet;
struct FixedLogicContext;

// The parts `rig` needs on a machine that does or does not present poses, for an
// entity that does or does not consume them (docs/gameplay/animation.md, "Taking part").
[[nodiscard]] AnimRigParts AnimRigRequiredParts(const AnimBoundRig& rig, bool presentsPose, bool consumesPose);

// Queues the adds and removes that take `entity` from `from` to `to`.
void ComposeAnimRigParts(EntityId entity, AnimRigParts from, AnimRigParts to, CommandBuffer& commands);

// The one owner of which animation components a rigged entity carries: it composes
// each entity to its rig's parts whenever those change, and takes them back from an
// entity whose rig is gone. Runs before anything reads them.
class AnimRigCompositionSystem
{
public:
    explicit AnimRigCompositionSystem(bool presentsPose, LoggingProvider* logging = nullptr);

    void FixedLogic(FixedLogicContext& ctx);
    // Outside a schedule: the preview and tests.
    void Compose(World& world);

private:
    void ComposeImpl(World& world, const StoragePartitionSet* partitions);

    // Reports a rig's diagnostics once per binding generation: every rigged entity,
    // whatever it carries, is composed here first.
    void Report(const AnimBoundRig& rig);

    bool PresentsPose = true;
    LoggingProvider* Logging = nullptr;
    std::unordered_set<std::uint64_t> Reported;
    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Write<AnimRigComposition>, With<AnimPoseConsumer>>> Consumed;
    std::optional<Query<Read<AnimRig>, Write<AnimRigComposition>, Without<AnimPoseConsumer>>> Unconsumed;
    std::optional<Query<Read<AnimRigComposition>, Without<AnimRig>>> Orphans;
};
