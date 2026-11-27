#include <logic/CounterVocabulary.h>

#include <authored/AuthoredApi.h>
#include <authored/AuthoredEventDispatcher.h>
#include <authored/AuthoredValueTraits.h>
#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Counter.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace
{
    constexpr std::int64_t kLargest = std::numeric_limits<std::int32_t>::max();
    constexpr std::int64_t kSmallest = std::numeric_limits<std::int32_t>::min();

    DataFieldSchema NameField()
    {
        return MakeDataField(DataFieldKind::GameplayTag, "name", "Name", "Which counter.");
    }

    std::int64_t SaturatingSum(std::int64_t current, std::int64_t amount)
    {
        constexpr std::int64_t kMost = std::numeric_limits<std::int64_t>::max();
        constexpr std::int64_t kLeast = std::numeric_limits<std::int64_t>::min();
        if (amount > 0 && current > kMost - amount)
            return kMost;
        if (amount < 0 && current < kLeast - amount)
            return kLeast;
        return current + amount;
    }

    CounterSaturation& SaturationOf(World& world)
    {
        return world.HasResource<CounterSaturation>() ? world.GetResource<CounterSaturation>()
                                                      : world.AddResource<CounterSaturation>();
    }

}

void DeclareCounterVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries,
                              AuthoredEventRegistrationScope& events)
{
    DeclareAuthoredApi<CounterOperations>(&verbs, &queries);

    AuthoredEventDefinition changed;
    changed.Name = std::string(kCounterChangedEvent);
    changed.DisplayName = "Counter changed";
    changed.Description = "A named number took a new value, zero when unset.";
    changed.Category = "Counters";
    changed.Payload.Children = { NameField(), MakeDataField(DataFieldKind::Int, "value", "Value"),
                                 MakeDataField(DataFieldKind::Entity, "holder", "Holder",
                                               "Whose counter; invalid for the world's.") };
    (void)events.Declare(std::move(changed));
}

EntityId FindCounter(const World& world, GameplayTagId name, EntityId holder)
{
    const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !world.IsRegistered<Counter>())
        return {};
    return index->FindKeyed(holder, CounterKey(name));
}

std::int32_t CounterValue(const World& world, GameplayTagId name, EntityId holder)
{
    const EntityId record = FindCounter(world, name, holder);
    return record.IsValid() ? world.TryGet<Counter>(record)->Value : 0;
}

CounterWrite WriteCounter(World& world, GameplayTagId name, EntityId holder, std::int64_t value)
{
    CounterWrite write;
    if (!IsSimulationAuthority(world) || !name.IsValid() || (holder.IsValid() && !world.IsAlive(holder)))
        return write;
    if (value > kLargest)
        write.Saturated = CounterBound::Maximum;
    else if (value < kSmallest)
        write.Saturated = CounterBound::Minimum;
    if (write.Saturated == CounterBound::Maximum)
        ++SaturationOf(world).AtMaximum;
    else if (write.Saturated == CounterBound::Minimum)
        ++SaturationOf(world).AtMinimum;
    write.Value = static_cast<std::int32_t>(std::clamp(value, kSmallest, kLargest));

    const EntityId record = FindCounter(world, name, holder);
    const std::int32_t current = record.IsValid() ? std::as_const(world).TryGet<Counter>(record)->Value : 0;
    if (write.Value == current)
        return write;
    write.Changed = true;
    if (write.Value == 0)
        world.DestroyEntity(record);
    else if (record.IsValid())
    {
        world.TryGet<Counter>(record)->Value = write.Value;
        NoteHeldRecordChanged(world, record);
    }
    else
    {
        ArchetypeSignature counter;
        counter.set(world.GetComponentId<Counter>());
        if (const EntityId created = PlaceHeldRecord(world, holder, counter, std::uint16_t{ 0 }); created.IsValid())
            (void)world.InitializeComponent<Counter>(created, Counter{ .Name = name, .Value = write.Value });
    }
    return write;
}

CounterOperations::CounterOperations(World& world, Logger& log, AuthoredEventDispatcher* events)
    : Entities(&world)
    , Log(&log)
    , Events(events)
{
}

VerbAnswer CounterOperations::Admit(GameplayTagId name, std::optional<EntityId> holder) const
{
    if (!name.IsValid())
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0);
    if (holder.has_value() && holder->IsValid() && !Entities->IsAlive(*holder))
        return VerbArgumentAnswer(VerbAdmission::UnresolvedReference, 2);
    if (!Entities->IsRegistered<Counter>() || !Entities->IsRegistered<Held>() || !Entities->HasResource<HeldRecordIndex>())
        return VerbAdmission::Unavailable;
    return VerbAnswer{};
}

VerbAnswer CounterOperations::Set(const VerbInvocation& invocation, GameplayTagId name, std::int64_t value,
                                  std::optional<EntityId> holder)
{
    if (const VerbAnswer admitted = Admit(name, holder); !admitted.Accepted())
        return admitted;
    return Write(invocation, name, holder.value_or(EntityId{}), value);
}

VerbAnswer CounterOperations::Add(const VerbInvocation& invocation, GameplayTagId name, std::int64_t amount,
                                  std::optional<EntityId> holder)
{
    if (const VerbAnswer admitted = Admit(name, holder); !admitted.Accepted())
        return admitted;
    const std::int64_t current = CounterValue(*Entities, name, holder.value_or(EntityId{}));
    return Write(invocation, name, holder.value_or(EntityId{}), SaturatingSum(current, amount));
}

std::optional<std::int64_t> CounterOperations::Value(GameplayTagId name, std::optional<EntityId> holder) const
{
    if (holder.has_value() && holder->IsValid() && !Entities->IsAlive(*holder))
        return std::nullopt;
    return CounterValue(*Entities, name, holder.value_or(EntityId{}));
}

VerbAdmission CounterOperations::Write(const VerbInvocation& invocation, GameplayTagId name, EntityId holder,
                                       std::int64_t value)
{
    const CounterWrite write = WriteCounter(*Entities, name, holder, value);
    if (write.Saturated != CounterBound::None)
    {
        const CounterSaturation& saturation = Entities->GetResource<CounterSaturation>();
        const bool first = write.Saturated == CounterBound::Maximum ? saturation.AtMaximum == 1
                                                                     : saturation.AtMinimum == 1;
        const GameplayTagRegistry* tags = Entities->TryGetResource<GameplayTagRegistry>();
        if (first)
            Log->Warn("counter '{}' was held at {}, the {} a counter keeps; later writes past a bound are counted, "
                      "not logged",
                      tags != nullptr ? tags->GetName(name) : std::string_view("?"), write.Value,
                      write.Saturated == CounterBound::Maximum ? "largest value" : "smallest value");
    }
    if (write.Changed && Events != nullptr)
        (void)Events->Publish(holder, CounterChanged{ .Name = name, .Value = write.Value, .Holder = holder },
                              invocation.Id);
    return VerbAdmission::Accepted;
}
