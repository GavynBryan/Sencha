#include <anim/AnimPoseSystem.h>

#include <anim/AnimDecisionLog.h>
#include <anim/AnimPosePool.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimSelectorState.h>
#include <anim/SkeletonCache.h>
#include <app/GameContexts.h>
#include <ecs/CommandBuffer.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <jobs/JobSystem.h>
#include <world/SimulationAuthority.h>

#include <algorithm>

namespace
{
    // Entities per job: enough to amortize the hand-off, few enough to spread
    // a small crowd across the workers.
    constexpr std::size_t kEntitiesPerJob = 8;
}

AnimPoseSystem::AnimPoseSystem(JobSystem* jobs)
    : Jobs(jobs)
{
}

AnimPoseSystem::~AnimPoseSystem() = default;

void AnimPoseSystem::PostFixed(PostFixedContext& ctx)
{
    PoseImpl(ctx.Entities, &ctx.Partitions, AuthorityTickOf(ctx.Entities, ctx.Time.TickIndex), ctx.Time.DeltaSeconds);
}

void AnimPoseSystem::Pose(World& world, AnimTick now, double tickSeconds)
{
    PoseImpl(world, nullptr, now, tickSeconds);
}

void AnimPoseSystem::PoseImpl(World& world, const StoragePartitionSet* partitions, AnimTick now, double tickSeconds)
{
    Items.clear();
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>() || !world.IsRegistered<AnimPoseState>()
        || !world.IsRegistered<AnimContentState>())
        return;
    if (!world.HasResource<AnimPosePool>())
        world.AddResource<AnimPosePool>();
    if (LastWorld != &world)
    {
        Unposed.reset();
        Posing.reset();
        LastWorld = &world;
    }
    if (!Unposed)
        Unposed.emplace(world);
    if (!Posing)
        Posing.emplace(world);

    const auto each = [&](auto& query, auto&& fn) {
        if (partitions != nullptr)
            query.ForEachChunkIn(*partitions, fn);
        else
            query.ForEachChunk(fn);
    };

    // A rig becomes posed on its first pass; the structural change waits for
    // the query to finish.
    {
        CommandBuffer commands(world);
        each(*Unposed, [&](auto& view) {
            for (std::uint32_t i = 0; i < view.Count(); ++i)
                commands.AddComponent(view.Entity(i), AnimPoseState{});
        });
        commands.Flush();
    }

    // Owner thread: bindings resolve through a shared cache, and slots are
    // assigned and shaped here so the jobs below never resize storage.
    AnimPosePool& pool = world.GetResource<AnimPosePool>();
    const World& reader = world;
    const bool hasSelection = world.IsRegistered<AnimSelectorState>();
    const bool hasLog = world.IsRegistered<AnimDecisionLog>();
    each(*Posing, [&](auto& view) {
        const auto rigs = view.template Read<AnimRig>();
        const auto contents = view.template Read<AnimContentState>();
        auto states = view.template Write<AnimPoseState>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = bindings->Resolve(rigs[i].Rig, world);
            const SkeletonData* skeleton = rig != nullptr && rig->Valid && rig->Skeleton.IsValid()
                    && bindings->SkeletonSource() != nullptr
                ? bindings->SkeletonSource()->Get(rig->Skeleton)
                : nullptr;
            if (skeleton == nullptr || skeleton->Joints.empty())
                continue;
            const EntityId entity = view.Entity(i);
            AnimPoseState& state = states[i];
            if (pool.Find(state.Slot) == nullptr)
                state.Slot = pool.Allocate(entity);
            pool.Shape(state.Slot, static_cast<std::uint32_t>(skeleton->Joints.size()),
                       static_cast<std::uint32_t>(std::min(rig->Layers.size(), kAnimMaxLayers)));
            pool.Find(state.Slot)->Skeleton = rig->Skeleton;

            Item item;
            item.Input.Sources = AnimPoseSources{ rig, bindings->ClipSource(), skeleton };
            item.Input.Content = &contents[i];
            item.Input.Selection = hasSelection ? reader.TryGet<AnimSelectorState>(entity) : nullptr;
            item.Input.State = &state;
            item.SlotHandle = state.Slot;
            item.Input.Log = hasLog ? world.TryGet<AnimDecisionLog>(entity) : nullptr;
            item.Input.Now = now;
            item.Input.TickSeconds = tickSeconds;
            Items.push_back(item);
        }
    });

    for (Item& item : Items)
        item.Input.Slot = pool.Find(item.SlotHandle);

    // Every entity reads only shared immutable data and writes only its own
    // components and slot, so the jobs need no ordering between them.
    const std::size_t workers = Jobs != nullptr ? Jobs->WorkerCount() + 1 : 1;
    if (Scratch.size() < workers)
        Scratch.resize(workers);
    if (Jobs == nullptr || Jobs->WorkerCount() == 0 || Items.size() <= kEntitiesPerJob)
    {
        for (const Item& item : Items)
            EvaluateAnimPose(item.Input, Scratch[0]);
        return;
    }
    const auto jobs = static_cast<std::uint32_t>((Items.size() + kEntitiesPerJob - 1) / kEntitiesPerJob);
    Jobs->ParallelFor(jobs, [&](std::uint32_t job) {
        AnimPoseScratch& scratch = Scratch[Jobs->CurrentWorkerIndex()];
        const std::size_t begin = static_cast<std::size_t>(job) * kEntitiesPerJob;
        const std::size_t end = std::min(Items.size(), begin + kEntitiesPerJob);
        for (std::size_t i = begin; i < end; ++i)
            EvaluateAnimPose(Items[i].Input, scratch);
    });
}
