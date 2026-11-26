#include <inventory/InventoryVocabulary.h>

#include <authored/AuthoredApi.h>
#include <authored/AuthoredValue.h>
#include <authored/AuthoredValueTraits.h>
#include <core/serialization/Archive.h>
#include <ecs/World.h>
#include <inventory/InventoryComponents.h>
#include <inventory/ItemDefinitionData.h>
#include <time/SimClock.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePlacement.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace
{
    constexpr std::string_view kEntry = ComponentDefinition<ItemEntry>::Identity;
    constexpr std::string_view kContainer = ComponentDefinition<ItemContainer>::Identity;
    constexpr std::string_view kPickup = ComponentDefinition<ItemPickup>::Identity;

    DataFieldSchema DefinitionReference()
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::DataAssetRef, "definition", "Item");
        field.Reference.DataSubtype = std::string(kItemDefinitionType);
        return field;
    }

    DataFieldSchema Defaulted(DataFieldSchema field, DataDefaultValue fallback)
    {
        field.Default = std::move(fallback);
        field.Required = false;
        return field;
    }

    DataFieldSchema CountArgument(std::int64_t fallback, std::string summary)
    {
        return Defaulted(MakeDataField(DataFieldKind::Int, "count", "Count", std::move(summary)), fallback);
    }

    VerbDefinition Verb(std::string_view name, std::string label, std::string description,
                        std::vector<DataFieldSchema> arguments)
    {
        VerbDefinition verb =
            MakeGameplayVerb(name, std::move(label), std::move(description), "Inventory", std::move(arguments));
        verb.Refusals = VerbRefusalsOf<InventoryRefusal>();
        return verb;
    }

    ItemId DefinitionNamed(const World& world, const AssetRef* reference)
    {
        const ItemRegistry* items = world.TryGetResource<ItemRegistry>();
        return reference != nullptr && items != nullptr ? items->Find(reference->Path) : ItemId{};
    }

    std::uint32_t CountOf(std::int64_t count)
    {
        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(count, 0, std::numeric_limits<std::uint32_t>::max()));
    }

    DataFieldSchema EntryResult(std::string summary)
    {
        DataFieldSchema entry = AuthoredField<std::optional<EntityId>>("entry", "Entry", std::move(summary));
        entry.Children.front().Reference.ComponentIdentity = std::string(kEntry);
        return entry;
    }

    VerbAnswer Answered(const VerbInvocation& invocation, const InventoryResult& result)
    {
        if (invocation.Result != nullptr && result.Refusal == InventoryRefusal::None && result.Entry.IsValid())
            *invocation.Result = AuthoredValue::Entity(result.Entry);
        return VerbAnswerOf(result.Refusal);
    }

    std::optional<ItemFilter> FilterOf(const World& world, const AssetRef* definition, std::optional<GameplayTagId> tag,
                                       bool within)
    {
        ItemFilter filter{ .Definition = DefinitionNamed(world, definition), .Tag = tag.value_or(GameplayTagId{}),
                           .Within = within };
        if (definition != nullptr && !filter.Definition.IsValid())
            return std::nullopt;
        return filter;
    }

    AuthoredQueryStatus CountUnits(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        EntityId container;
        const AssetRef* definition = nullptr;
        std::optional<GameplayTagId> tag;
        bool within = false;
        if (arguments.size() < 4 || !arguments[0].TryGetEntity(container)
            || (!arguments[1].IsNone() && !arguments[1].TryGetDataAsset(definition))
            || !ReadAuthoredArgument(arguments, 2, tag) || !arguments[3].TryGetBool(within))
            return AuthoredQueryStatus::InvalidArguments;
        if (!world.IsAlive(container))
            return AuthoredQueryStatus::Unavailable;
        const std::optional<ItemFilter> filter = FilterOf(world, definition, tag, within);
        result = AuthoredValue::Int(
            filter.has_value() ? static_cast<std::int64_t>(CountItemUnits(world, container, *filter)) : 0);
        return AuthoredQueryStatus::Value;
    }
}

DataFieldSchema ItemChangeArgument()
{
    return Defaulted(MakeDataField(DataFieldKind::Bool, "force", "Force",
                                   "What the World decides (a death's loot): no layer above can refuse it."),
                     false);
}

ItemChangeClass ItemChangeOf(const VerbInvocation& invocation, std::size_t slot)
{
    bool forced = false;
    return invocation.Arguments != nullptr && invocation.Arguments->TryGetBool(slot, forced) && forced
        ? ItemChangeClass::Forced
        : ItemChangeClass::Requested;
}

void DeclareInventoryVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries)
{
    const auto into = [] { return MakeEntityArgument("into", "Into", kContainer, "The container it goes into."); };
    VerbDefinition take = Verb(kInventoryTakeVerb, "Take item", "Takes a pickup into a container; the pickup goes.",
                               { MakeEntityArgument("pickup", "Pickup", kPickup, "What is taken."), into() });
    take.Result = EntryResult("The entry it went into; absent for an item that is not kept.");
    (void)verbs.Declare(std::move(take));
    VerbDefinition give = Verb(kInventoryGiveVerb, "Give item", "Puts units of an item into a container, all or none of them.",
                               { into(), DefinitionReference(), CountArgument(1, "How many units.") });
    give.Result = EntryResult("The entry the units went into: a new one, or the stack they joined.");
    (void)verbs.Declare(std::move(give));
    (void)verbs.Declare(Verb(kInventoryTransferVerb, "Move item", "Moves an entry, or part of a stack, into a container.",
                             { MakeEntityArgument("entry", "Entry", kEntry, "What moves."), into(),
                               CountArgument(0, "How many units; zero moves all of it."), ItemChangeArgument() }));
    DataFieldSchema at = MakeDataField(DataFieldKind::Vector, "at", "At", "Where it lands.");
    at.Required = false;
    (void)verbs.Declare(Verb(kInventoryDropVerb, "Drop item",
                             "Takes an entry out of its container into a pickup, in the zone where it lands.",
                             { MakeEntityArgument("entry", "Entry", kEntry, "What is dropped."), std::move(at),
                               ItemChangeArgument(),
                               Defaulted(MakeDataField(DataFieldKind::Bool, "keep", "Keep",
                                                       "The pickup stays with its zone, across its unload and in "
                                                       "saves, until it is taken."),
                                         true) }));
    (void)verbs.Declare(Verb(kInventoryUseVerb, "Use item",
                             "Runs the item's use, with the entry as its source, in the next ability pass.",
                             { MakeEntityArgument("entry", "Entry", kEntry, "What is used."),
                               AuthoredField<std::optional<EntityId>>("user", "User",
                                                                      "Who uses it; whoever holds it when absent.") }));

    DataFieldSchema definition = MakeDataField(DataFieldKind::Optional, "definition", "Item", "Counts only this item.");
    definition.Required = false;
    definition.Children.push_back(DefinitionReference());
    AuthoredQueryDefinition count;
    count.Name = std::string(kInventoryCountQuery);
    count.DisplayName = "Item count";
    count.Description = "How many units a container holds, spent entries not counted.";
    count.Category = "Inventory";
    count.Arguments.Children = {
        MakeEntityArgument("container", "Container", kContainer, "Whose items are counted."), std::move(definition),
        AuthoredField<std::optional<GameplayTagId>>("tag", "Tag", "Counts only items carrying this tag or one beneath it."),
        Defaulted(MakeDataField(DataFieldKind::Bool, "within", "Within", "Counts what its entries hold too, however deep."),
                  false)
    };
    count.Result = MakeDataField(DataFieldKind::Int, "count", "Count");
    (void)queries.Declare(std::move(count));
    DeclareAuthoredApi<InventoryOperations>(&verbs, &queries);
}

VerbAnswer InventoryOperations::Take(const VerbInvocation& invocation)
{
    EntityId pickup;
    EntityId into;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, pickup)
        || !invocation.Arguments->TryGetEntity(1, into))
        return VerbAdmission::InvalidArguments;
    return Answered(invocation, Inventory(*Entities, Clock->TimeOfTick(invocation.Tick)).Take(pickup, into));
}

VerbAnswer InventoryOperations::Give(const VerbInvocation& invocation)
{
    EntityId into;
    const AssetRef* reference = nullptr;
    std::int64_t count = 1;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, into)
        || !invocation.Arguments->TryGetDataAsset(1, reference) || !invocation.Arguments->TryGetInt(2, count))
        return VerbAdmission::InvalidArguments;
    const ItemId definition = DefinitionNamed(*Entities, reference);
    if (!definition.IsValid())
        return VerbAdmission::InvalidArguments;
    return Answered(invocation,
                    Inventory(*Entities, Clock->TimeOfTick(invocation.Tick)).Give(into, definition, CountOf(count)));
}

VerbAnswer InventoryOperations::Transfer(const VerbInvocation& invocation)
{
    EntityId entry;
    EntityId into;
    std::int64_t count = 0;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, entry)
        || !invocation.Arguments->TryGetEntity(1, into) || !invocation.Arguments->TryGetInt(2, count))
        return VerbAdmission::InvalidArguments;
    return VerbAnswerOf(Inventory(*Entities, Clock->TimeOfTick(invocation.Tick)).Transfer(entry, into, CountOf(count), ItemChangeOf(invocation, 3)).Refusal);
}

VerbAnswer InventoryOperations::Drop(const VerbInvocation& invocation)
{
    EntityId entry;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, entry))
        return VerbAdmission::InvalidArguments;
    AuthoredVectorValue at;
    Transform3f where = Transform3f::Identity();
    if (invocation.Arguments->TryGetVector(1, at))
        where.Position = Vec3d(at.Components[0], at.Components[1], at.Components[2]);
    const EntityId holder = ItemHolder(*Entities, entry);
    StoragePartitionId partition =
        Entities->IsAlive(holder) ? Entities->GetEntityPartition(holder) : StoragePartitionId::Default();
    if (const ZonePlacement* zones = Entities->TryGetResource<ZonePlacement>(); zones != nullptr && zones->PartitionAt)
        if (const std::optional<StoragePartitionId> landed = zones->PartitionAt(where.Position))
            partition = *landed;
    const InventoryResult dropped = Inventory(*Entities, Clock->TimeOfTick(invocation.Tick))
                                        .Drop(entry, where, partition, ItemChangeOf(invocation, 2));
    bool keep = true;
    (void)invocation.Arguments->TryGetBool(3, keep);
    if (dropped.Refusal == InventoryRefusal::None && keep)
        (void)KeepWithZone(*Entities, dropped.Entry);
    return VerbAnswerOf(dropped.Refusal);
}

VerbAnswer InventoryOperations::Use(const VerbInvocation& invocation)
{
    EntityId entry;
    std::optional<EntityId> user;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, entry)
        || !ReadAuthoredArgument(*invocation.Arguments, 1, user))
        return VerbAdmission::InvalidArguments;
    return VerbAnswerOf(Inventory(*Entities, Clock->TimeOfTick(invocation.Tick)).Use(user.value_or(ItemHolder(*Entities, entry)), entry).Refusal);
}

std::optional<EntityId> InventoryOperations::Entry(EntityId container, std::optional<AuthoredDataRef> definition,
                                                   std::optional<GameplayTagId> tag, bool within, bool last) const
{
    const std::optional<ItemFilter> filter =
        FilterOf(*Entities, definition.has_value() ? definition->Reference : nullptr, tag, within);
    const EntityId entry = filter.has_value()
        ? FindItemEntry(*Entities, container, *filter, last ? ItemEntryPick::Last : ItemEntryPick::First)
        : EntityId{};
    return entry.IsValid() ? std::optional<EntityId>(entry) : std::nullopt;
}

std::optional<double> InventoryOperations::Property(EntityId entry, GameplayTagId property) const
{
    const ItemEntry* held = Entities->IsAlive(entry) ? Entities->TryGet<ItemEntry>(entry) : nullptr;
    const ItemDefinition* item = held != nullptr ? FindItemDefinition(*Entities, held->Definition) : nullptr;
    return item != nullptr ? ItemNumberOf(*item, property) : std::nullopt;
}

std::optional<double> InventoryOperations::Total(EntityId container, GameplayTagId property,
                                                 std::optional<AuthoredDataRef> definition,
                                                 std::optional<GameplayTagId> tag, bool within) const
{
    if (!Entities->IsAlive(container))
        return std::nullopt;
    const std::optional<ItemFilter> filter =
        FilterOf(*Entities, definition.has_value() ? definition->Reference : nullptr, tag, within);
    return filter.has_value() ? ItemPropertyTotal(*Entities, container, property, *filter) : 0.0;
}

AuthoredQueryBindingToken BindInventoryQuery(AuthoredQueryDispatcher& dispatcher, const World& world)
{
    return dispatcher.Bind<&CountUnits>(dispatcher.Registry().Find(kInventoryCountQuery), world);
}
