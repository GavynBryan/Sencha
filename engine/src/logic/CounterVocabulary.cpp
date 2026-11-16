#include <logic/CounterVocabulary.h>

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

    DataFieldSchema HolderField()
    {
        return AuthoredField<std::optional<EntityId>>(
            "holder", "Holder", "Whose counter, such as a participant's body. The world's when absent.");
    }

    VerbDefinition Verb(std::string_view name, std::string label, std::string description, DataFieldSchema number)
    {
        VerbDefinition verb;
        verb.Name = std::string(name);
        verb.DisplayName = std::move(label);
        verb.Description = std::move(description);
        verb.Category = "Counters";
        verb.Arguments.Children = { NameField(), std::move(number), HolderField() };
        return verb;
    }

    bool HolderGone(const World& world, const std::optional<EntityId>& holder)
    {
        return holder.has_value() && holder->IsValid() && !world.IsAlive(*holder);
    }

    struct Request
    {
        GameplayTagId Name;
        std::int64_t Number = 0;
        EntityId Holder;
    };

    VerbAdmission Read(const World& world, const VerbInvocation& invocation, Request& out)
    {
        std::optional<EntityId> holder;
        if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetTag(0, out.Name)
            || !invocation.Arguments->TryGetInt(1, out.Number) || !ReadAuthoredArgument(*invocation.Arguments, 2, holder)
            || !out.Name.IsValid() || HolderGone(world, holder))
            return VerbAdmission::InvalidArguments;
        out.Holder = holder.value_or(EntityId{});
        if (!IsSimulationAuthority(world))
            return VerbAdmission::Refused;
        if (!world.IsRegistered<Counter>() || !world.IsRegistered<Held>() || !world.HasResource<HeldRecordIndex>())
            return VerbAdmission::Unavailable;
        return VerbAdmission::Accepted;
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

    AuthoredQueryStatus EvaluateValue(const World& world, std::span<const AuthoredValue> arguments,
                                      AuthoredValue& result)
    {
        GameplayTagId name;
        std::optional<EntityId> holder;
        if (!ReadAuthoredArgument(arguments, 0, name) || !ReadAuthoredArgument(arguments, 1, holder) || !name.IsValid())
            return AuthoredQueryStatus::InvalidArguments;
        if (HolderGone(world, holder))
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Int(CounterValue(world, name, holder.value_or(EntityId{})));
        return AuthoredQueryStatus::Value;
    }
}

void DeclareCounterVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries,
                              AuthoredEventRegistrationScope& events)
{
    (void)verbs.Declare(Verb(kCounterSetVerb, "Set counter",
                             "Sets a named number, kept by saves. Zero unsets it. Values past a 32-bit integer are "
                             "held at its bound.",
                             AuthoredField<std::int64_t>("value", "Value", "What it becomes.")));
    DataFieldSchema amount = AuthoredField<std::int64_t>("amount", "Amount", "What is added; negative takes away.");
    amount.Required = false;
    amount.Default = std::int64_t{ 1 };
    (void)verbs.Declare(Verb(kCounterAddVerb, "Add to counter",
                             "Adds to a named number, kept by saves. Reaching zero unsets it. Values past a 32-bit "
                             "integer are held at its bound.",
                             std::move(amount)));

    AuthoredQueryDefinition value;
    value.Name = std::string(kCounterValueQuery);
    value.DisplayName = "Counter value";
    value.Description = "A named number; zero when it was never set.";
    value.Category = "Counters";
    value.Arguments.Children = { NameField(), HolderField() };
    value.Result = MakeDataField(DataFieldKind::Int, "value", "Value");
    (void)queries.Declare(std::move(value));

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
    for (const EntityId record : index->RecordsOf(holder))
        if (const Counter* counter = world.TryGet<Counter>(record); counter != nullptr && counter->Name == name)
            return record;
    return {};
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
        world.GetResource<HeldRecordIndex>().NoteChanged(record);
    }
    else
    {
        const EntityId created = world.CreateEntity(PersistentStoragePartition);
        world.AddComponent<Counter>(created, Counter{ .Name = name, .Value = write.Value });
        HoldRecord(world, created, holder, std::uint16_t{ 0 });
    }
    return write;
}

CounterOperations::CounterOperations(World& world, Logger& log, AuthoredEventDispatcher* events)
    : Entities(&world)
    , Log(&log)
    , Events(events)
{
}

VerbAdmission CounterOperations::Set(const VerbInvocation& invocation)
{
    Request request;
    if (const VerbAdmission read = Read(*Entities, invocation, request); read != VerbAdmission::Accepted)
        return read;
    return Write(invocation, request.Name, request.Holder, request.Number);
}

VerbAdmission CounterOperations::Add(const VerbInvocation& invocation)
{
    Request request;
    if (const VerbAdmission read = Read(*Entities, invocation, request); read != VerbAdmission::Accepted)
        return read;
    const std::int64_t current = CounterValue(*Entities, request.Name, request.Holder);
    return Write(invocation, request.Name, request.Holder, SaturatingSum(current, request.Number));
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

AuthoredQueryBindingToken BindCounterQuery(AuthoredQueryDispatcher& dispatcher, const World& world)
{
    return dispatcher.Bind<&EvaluateValue>(dispatcher.Registry().Find(kCounterValueQuery), world);
}
