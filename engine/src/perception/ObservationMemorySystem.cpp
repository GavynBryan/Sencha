#include <perception/ObservationMemorySystem.h>

#include <gameplay_tags/GameplayTagRegistry.h>
#include <perception/ObservationSharing.h>
#include <perception/PerceptionFrame.h>
#include <perception/PerceptionVocabulary.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

namespace
{
    // A subject observed again within this many seconds is still being
    // tracked, and the two positions measure its velocity.
    constexpr float kContinuityGap = 0.25f;
    // Anonymous observations closer than this to a remembered one refresh it.
    constexpr float kAnonymousMergeDistance = 2.0f;

    ObservationMemoryEntry* Match(ObservationMemory& memory, const Observation& observation)
    {
        ObservationMemoryEntry* nearest = nullptr;
        float nearestDistance = kAnonymousMergeDistance * kAnonymousMergeDistance;
        for (std::uint32_t index = 0; index < memory.Count; ++index)
        {
            ObservationMemoryEntry& entry = memory.Entries[index];
            if (observation.Subject.IsValid())
            {
                if (entry.Subject == observation.Subject)
                    return &entry;
                continue;
            }
            if (entry.Subject.IsValid())
                continue;
            const float distance = (entry.Position - observation.Position).SqrMagnitude();
            if (distance <= nearestDistance)
            {
                nearest = &entry;
                nearestDistance = distance;
            }
        }
        return nearest;
    }

    ObservationMemoryEntry& Slot(ObservationMemory& memory)
    {
        if (memory.Count < kObservationMemoryCapacity)
        {
            memory.Entries[memory.Count] = {};
            return memory.Entries[memory.Count++];
        }
        ObservationMemoryEntry* oldest = &memory.Entries[0];
        for (std::uint32_t index = 1; index < memory.Count; ++index)
            if (memory.Entries[index].Age > oldest->Age)
                oldest = &memory.Entries[index];
        *oldest = {};
        return *oldest;
    }

    // The way `subject` faces now, which is when it is being observed; zero for none.
    Vec3d FacingOf(const World& world, EntityId subject)
    {
        if (!subject.IsValid() || !world.IsAlive(subject))
            return Vec3d::Zero();
        const WorldTransform* pose = world.TryGet<WorldTransform>(subject);
        return pose != nullptr ? pose->Value.Rotation.RotateVector(Vec3d::Forward()) : Vec3d::Zero();
    }

    void Remember(ObservationMemory& memory, const Observation& observation, const Vec3d& facing)
    {
        ObservationMemoryEntry* entry = Match(memory, observation);
        if (entry == nullptr)
        {
            entry = &Slot(memory);
            entry->Subject = observation.Subject;
        }
        else if (entry->Age == 0.0f)
        {
            if (observation.Confidence <= entry->Confidence)
                return;
        }
        else if (entry->Age <= kContinuityGap)
        {
            entry->Velocity = (observation.Position - entry->Position) / entry->Age;
            entry->Tracked += entry->Age;
        }
        else
        {
            entry->Velocity = Vec3d::Zero();
            entry->Tracked = 0.0f;
        }
        entry->Sense = observation.Sense;
        entry->Position = observation.Position;
        entry->Facing = facing;
        entry->Confidence = observation.Confidence;
        entry->Age = 0.0f;
    }

    void Forget(ObservationMemory& memory)
    {
        std::uint32_t kept = 0;
        for (std::uint32_t index = 0; index < memory.Count; ++index)
            if (memory.Entries[index].Age <= memory.RetainSeconds)
                memory.Entries[kept++] = memory.Entries[index];
        for (std::uint32_t index = kept; index < memory.Count; ++index)
            memory.Entries[index] = {};
        memory.Count = kept;
    }
}

void ObservationMemorySystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    const PerceptionFrame* frame = world.TryGetResource<PerceptionFrame>();
    if (frame == nullptr || !world.IsRegistered<ObservationMemory>() || !IsSimulationAuthority(world))
        return;
    if (QueriedWorld != &world)
    {
        Memories.emplace(world);
        QueriedWorld = &world;
        InFlight.clear();
    }

    const float elapsed = static_cast<float>(ctx.Time.DeltaSeconds);
    Memories->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        auto memories = view.template Write<ObservationMemory>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
        {
            ObservationMemory& memory = memories[row];
            for (std::uint32_t index = 0; index < memory.Count; ++index)
                memory.Entries[index].Age += elapsed;
            for (const SenseScan& scan : frame->ScansOf(view.Entity(row)))
                for (const Observation& observation : frame->ObservationsOf(scan))
                    Remember(memory, observation, FacingOf(world, observation.Subject));
        }
    });
    Now += elapsed;
    if (world.IsRegistered<ObservationSharing>())
        Share(world, ctx);
    Memories->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        auto memories = view.template Write<ObservationMemory>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            Forget(memories[row]);
    });
}

void ObservationMemorySystem::Share(World& world, const FixedLogicContext&)
{
    const PerceptionFrame& frame = world.GetResource<PerceptionFrame>();
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const GameplayTagId shared = EnsurePerceptionSenses(world).Shared;
    if (tags == nullptr)
        return;

    struct Member
    {
        EntityId Entity;
        GameplayTagId Group;
        Vec3d Position;
        const ObservationSharing* Sharing;
    };
    std::vector<Member> members;
    const World& view = world;
    view.ForEachComponent<ObservationSharing>([&](EntityId entity, const ObservationSharing& sharing) {
        const WorldTransform* pose = view.TryGet<WorldTransform>(entity);
        const GameplayTagId group = tags->FindTag(sharing.Group.View());
        if (pose != nullptr && group.IsValid() && view.HasComponent<ObservationMemory>(entity))
            members.push_back({ entity, group, pose->Value.Position, &sharing });
    });
    // In identity order, so what arrives where is the same on every run.
    std::ranges::sort(members, {}, &Member::Entity);

    for (const Member& member : members)
        for (const SenseScan& scan : frame.ScansOf(member.Entity))
            for (const Observation& observation : frame.ObservationsOf(scan))
                InFlight.push_back({ .Group = member.Group,
                                     .From = member.Entity,
                                     .FromPosition = member.Position,
                                     .Radius = member.Sharing->Radius,
                                     .Due = Now + member.Sharing->DelaySeconds,
                                     .Seen = observation,
                                     .Facing = FacingOf(world, observation.Subject) });

    Arrived.clear();
    std::erase_if(InFlight, [&](const SharedObservation& pending) {
        if (pending.Due > Now + 1.0e-9)
            return false;
        Arrived.push_back(pending);
        return true;
    });
    for (const SharedObservation& arrival : Arrived)
    {
        Observation passed = arrival.Seen;
        passed.Sense = shared;
        for (const Member& member : members)
        {
            if (member.Group != arrival.Group || member.Entity == arrival.From)
                continue;
            if (arrival.Radius > 0.0f
                && (member.Position - arrival.FromPosition).SqrMagnitude() > arrival.Radius * arrival.Radius)
                continue;
            if (ObservationMemory* memory = world.TryGet<ObservationMemory>(member.Entity))
            {
                passed.Observer = member.Entity;
                Remember(*memory, passed, arrival.Facing);
            }
        }
    }
}
