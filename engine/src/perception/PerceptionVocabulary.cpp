#include <perception/PerceptionVocabulary.h>

#include <authored/AuthoredApi.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <perception/ObservationMemory.h>
#include <perception/PerceptionFrame.h>
#include <perception/PerceptionStimuli.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>

#include <limits>

namespace
{
    bool SubjectCarries(const World& world, const GameplayTagRegistry& tags, EntityId subject, GameplayTagId tag)
    {
        const GameplayTagContainer* container = world.TryGet<GameplayTagContainer>(subject);
        return container != nullptr && container->HasDescendantOf(tags, tag);
    }

    const SenseScan* SightScanOf(const World& world, EntityId observer, const PerceptionFrame*& frame)
    {
        frame = world.TryGetResource<PerceptionFrame>();
        const PerceptionSenses* senses = world.TryGetResource<PerceptionSenses>();
        return frame != nullptr && senses != nullptr ? frame->ScanOf(observer, senses->Sight) : nullptr;
    }

    std::optional<bool> Unseen(const SenseScan& scan)
    {
        return scan.Coverage == ScanCoverage::Complete ? std::optional<bool>(false) : std::nullopt;
    }

    const ObservationMemory* MemoryOf(const World& world, EntityId observer)
    {
        return IsSimulationAuthority(world) ? world.TryGet<ObservationMemory>(observer) : nullptr;
    }

    const ObservationMemory* Remembered(const World& world, EntityId observer, EntityId subject,
                                        const ObservationMemoryEntry*& entry)
    {
        const ObservationMemory* memory = MemoryOf(world, observer);
        entry = memory != nullptr && subject.IsValid() ? FindRemembered(*memory, subject) : nullptr;
        return memory;
    }
}

bool Sees(const World& world, EntityId observer, EntityId subject)
{
    const PerceptionFrame* frame = world.TryGetResource<PerceptionFrame>();
    const PerceptionSenses* senses = world.TryGetResource<PerceptionSenses>();
    return frame != nullptr && senses != nullptr && frame->Find(observer, senses->Sight, subject) != nullptr;
}

const PerceptionSenses& EnsurePerceptionSenses(World& world)
{
    if (!world.HasResource<GameplayTagRegistry>())
        world.AddResource<GameplayTagRegistry>();
    if (!world.HasResource<PerceptionSenses>())
    {
        GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
        const std::optional<GameplayTagId> sight = tags.RegisterTag(kSightSenseTag);
        const std::optional<GameplayTagId> hearing = tags.RegisterTag(kHearingSenseTag);
        const std::optional<GameplayTagId> shared = tags.RegisterTag(kSharedSenseTag);
        world.AddResource<PerceptionSenses>(PerceptionSenses{ .Sight = sight.value_or(GameplayTagId{}),
                                                              .Hearing = hearing.value_or(GameplayTagId{}),
                                                              .Shared = shared.value_or(GameplayTagId{}) });
    }
    return world.GetResource<PerceptionSenses>();
}

bool InstallPerceptionVocabulary(World& world)
{
    (void)EnsurePerceptionSenses(world);
    if (!world.HasResource<PerceptionFrame>())
        world.AddResource<PerceptionFrame>();

    AuthoredQueryRegistry* registry = FindAuthoredQueryRegistry(world);
    if (registry == nullptr)
        return false;
    AuthoredQueryRegistrationScope scope(*registry, "engine");
    DeclareAuthoredApi<PerceptionOperations>(nullptr, &scope);
    if (!scope.Commit())
        return false;

    VerbRegistry* verbs = FindVerbRegistry(world);
    if (verbs == nullptr)
        return false;
    VerbRegistrationScope verbScope(*verbs, "engine");
    DeclareAuthoredApi<PerceptionOperations>(&verbScope, nullptr);
    return verbScope.Commit();
}

VerbAdmission PerceptionOperations::Stimulus(std::optional<EntityId> source, std::optional<Vec3d> position,
                                             float radius, std::optional<EntityId> only)
{
    PerceptionStimulus stimulus;
    stimulus.Radius = radius;
    stimulus.Source = source.value_or(EntityId{});
    stimulus.Only = only.value_or(EntityId{});
    if (position.has_value())
        stimulus.Position = *position;
    else
    {
        const WorldTransform* at =
            stimulus.Source.IsValid() ? std::as_const(*Entities).TryGet<WorldTransform>(stimulus.Source) : nullptr;
        if (at == nullptr)
            return VerbAdmission::InvalidArguments;
        stimulus.Position = at->Value.Position;
    }
    return StagePerceptionStimulus(*Entities, stimulus) ? VerbAdmission::Accepted : VerbAdmission::Refused;
}

std::optional<bool> PerceptionOperations::Sees(EntityId observer, EntityId subject) const
{
    const PerceptionFrame* frame = nullptr;
    const SenseScan* scan = SightScanOf(*Entities, observer, frame);
    if (scan == nullptr)
        return std::nullopt;
    for (const Observation& observation : frame->ObservationsOf(*scan))
        if (observation.Subject == subject)
            return true;
    return Unseen(*scan);
}

std::optional<bool> PerceptionOperations::SeesTagged(EntityId observer, GameplayTagId tag) const
{
    const World& world = *Entities;
    const PerceptionFrame* frame = nullptr;
    const SenseScan* scan = SightScanOf(world, observer, frame);
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    if (tags == nullptr || scan == nullptr)
        return std::nullopt;
    for (const Observation& observation : frame->ObservationsOf(*scan))
        if (SubjectCarries(world, *tags, observation.Subject, tag))
            return true;
    return Unseen(*scan);
}

std::optional<EntityId> PerceptionOperations::NearestSeenTagged(EntityId observer, GameplayTagId tag) const
{
    const World& world = *Entities;
    const PerceptionFrame* frame = nullptr;
    const SenseScan* scan = SightScanOf(world, observer, frame);
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const WorldTransform* from = world.TryGet<WorldTransform>(observer);
    if (tags == nullptr || from == nullptr || scan == nullptr)
        return std::nullopt;

    EntityId nearest;
    float nearestDistance = std::numeric_limits<float>::infinity();
    for (const Observation& observation : frame->ObservationsOf(*scan))
    {
        if (!observation.Subject.IsValid() || !SubjectCarries(world, *tags, observation.Subject, tag))
            continue;
        const float distance = (observation.Position - from->Value.Position).SqrMagnitude();
        if (distance < nearestDistance || (distance == nearestDistance && observation.Subject < nearest))
        {
            nearest = observation.Subject;
            nearestDistance = distance;
        }
    }
    return nearest.IsValid() ? std::optional<EntityId>(nearest) : std::nullopt;
}

std::optional<bool> PerceptionOperations::Remembers(EntityId observer, EntityId subject) const
{
    const ObservationMemoryEntry* entry = nullptr;
    if (Remembered(*Entities, observer, subject, entry) == nullptr)
        return std::nullopt;
    return entry != nullptr;
}

std::optional<Vec3d> PerceptionOperations::LastSeenPosition(EntityId observer, EntityId subject) const
{
    const ObservationMemoryEntry* entry = nullptr;
    (void)Remembered(*Entities, observer, subject, entry);
    return entry != nullptr ? std::optional<Vec3d>(entry->Position) : std::nullopt;
}

std::optional<Vec3d> PerceptionOperations::LastSeenVelocity(EntityId observer, EntityId subject) const
{
    const ObservationMemoryEntry* entry = nullptr;
    (void)Remembered(*Entities, observer, subject, entry);
    return entry != nullptr ? std::optional<Vec3d>(entry->Velocity) : std::nullopt;
}

std::optional<float> PerceptionOperations::SecondsSinceSeen(EntityId observer, EntityId subject) const
{
    const ObservationMemoryEntry* entry = nullptr;
    (void)Remembered(*Entities, observer, subject, entry);
    return entry != nullptr ? std::optional<float>(entry->Age) : std::nullopt;
}

std::optional<float> PerceptionOperations::SecondsTracked(EntityId observer, EntityId subject) const
{
    const ObservationMemoryEntry* entry = nullptr;
    (void)Remembered(*Entities, observer, subject, entry);
    return entry != nullptr ? std::optional<float>(entry->Tracked) : std::nullopt;
}

std::optional<EntityId> PerceptionOperations::NearestRememberedTagged(EntityId observer, GameplayTagId tag) const
{
    const World& world = *Entities;
    const ObservationMemory* memory = MemoryOf(world, observer);
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const WorldTransform* from = world.TryGet<WorldTransform>(observer);
    if (memory == nullptr || tags == nullptr || from == nullptr)
        return std::nullopt;

    EntityId nearest;
    float nearestDistance = std::numeric_limits<float>::infinity();
    for (std::uint32_t index = 0; index < memory->Count; ++index)
    {
        const ObservationMemoryEntry& entry = memory->Entries[index];
        if (!entry.Subject.IsValid() || !world.IsAlive(entry.Subject)
            || !SubjectCarries(world, *tags, entry.Subject, tag))
            continue;
        const float distance = (entry.Position - from->Value.Position).SqrMagnitude();
        if (distance < nearestDistance || (distance == nearestDistance && entry.Subject < nearest))
        {
            nearest = entry.Subject;
            nearestDistance = distance;
        }
    }
    return nearest.IsValid() ? std::optional<EntityId>(nearest) : std::nullopt;
}
