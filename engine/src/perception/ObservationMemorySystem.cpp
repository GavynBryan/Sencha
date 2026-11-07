#include <perception/ObservationMemorySystem.h>

#include <perception/PerceptionFrame.h>
#include <world/SimulationAuthority.h>

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

    void Remember(ObservationMemory& memory, const Observation& observation)
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
                    Remember(memory, observation);
            Forget(memory);
        }
    });
}
