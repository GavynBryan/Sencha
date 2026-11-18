#include <inventory/InventoryVocabulary.h>

#include <authored/AuthoredValue.h>
#include <authored/AuthoredValueTraits.h>
#include <ecs/World.h>
#include <inventory/InventoryComponents.h>
#include <inventory/ItemDefinitionData.h>

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
        return MakeGameplayVerb(name, std::move(label), std::move(description), "Inventory", std::move(arguments));
    }

    VerbAdmission AdmissionOf(InventoryRefusal refusal)
    {
        return refusal == InventoryRefusal::None ? VerbAdmission::Accepted : VerbAdmission::Refused;
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

    struct CountFilter
    {
        const AssetRef* Definition = nullptr;
        std::optional<GameplayTagId> Tag;
        bool Within = false;
    };

    std::int64_t Units(const World& world, EntityId container, const CountFilter& filter)
    {
        const ItemId definition = DefinitionNamed(world, filter.Definition);
        if (filter.Definition != nullptr && !definition.IsValid())
            return 0;
        const ItemRegistry* items = world.TryGetResource<ItemRegistry>();
        std::int64_t units = 0;
        for (const EntityId entry : filter.Within ? HeldEntries(world, container) : ItemsIn(world, container))
        {
            const ItemEntry& item = *world.TryGet<ItemEntry>(entry);
            if (filter.Definition != nullptr && item.Definition != definition)
                continue;
            const ItemDefinition* described = items != nullptr ? items->Get(item.Definition) : nullptr;
            if (filter.Tag.has_value() && (described == nullptr || !ItemCarries(world, *described, *filter.Tag)))
                continue;
            units += item.Count;
        }
        return units;
    }

    AuthoredQueryStatus CountUnits(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        EntityId container;
        CountFilter filter;
        if (arguments.size() < 4 || !arguments[0].TryGetEntity(container)
            || (!arguments[1].IsNone() && !arguments[1].TryGetDataAsset(filter.Definition))
            || !ReadAuthoredArgument(arguments, 2, filter.Tag) || !arguments[3].TryGetBool(filter.Within))
            return AuthoredQueryStatus::InvalidArguments;
        if (!world.IsAlive(container))
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Int(Units(world, container, filter));
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
    (void)verbs.Declare(Verb(kInventoryTakeVerb, "Take item", "Takes a pickup into a container; the pickup goes.",
                             { MakeEntityArgument("pickup", "Pickup", kPickup, "What is taken."), into() }));
    (void)verbs.Declare(Verb(kInventoryGiveVerb, "Give item",
                             "Puts units of an item into a container, all or none of them.",
                             { into(), DefinitionReference(), CountArgument(1, "How many units.") }));
    (void)verbs.Declare(Verb(kInventoryTransferVerb, "Move item", "Moves an entry, or part of a stack, into a container.",
                             { MakeEntityArgument("entry", "Entry", kEntry, "What moves."), into(),
                               CountArgument(0, "How many units; zero moves all of it."), ItemChangeArgument() }));
    DataFieldSchema at = MakeDataField(DataFieldKind::Vector, "at", "At", "Where it lands.");
    at.Required = false;
    (void)verbs.Declare(Verb(kInventoryDropVerb, "Drop item", "Takes an entry out of its container into a pickup.",
                             { MakeEntityArgument("entry", "Entry", kEntry, "What is dropped."), std::move(at),
                               ItemChangeArgument() }));
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
}

VerbAdmission InventoryOperations::Take(const VerbInvocation& invocation)
{
    EntityId pickup;
    EntityId into;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, pickup)
        || !invocation.Arguments->TryGetEntity(1, into))
        return VerbAdmission::InvalidArguments;
    return AdmissionOf(Inventory(*Entities).Take(pickup, into).Refusal);
}

VerbAdmission InventoryOperations::Give(const VerbInvocation& invocation)
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
    return AdmissionOf(Inventory(*Entities).Give(into, definition, CountOf(count)).Refusal);
}

VerbAdmission InventoryOperations::Transfer(const VerbInvocation& invocation)
{
    EntityId entry;
    EntityId into;
    std::int64_t count = 0;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, entry)
        || !invocation.Arguments->TryGetEntity(1, into) || !invocation.Arguments->TryGetInt(2, count))
        return VerbAdmission::InvalidArguments;
    return AdmissionOf(Inventory(*Entities).Transfer(entry, into, CountOf(count), ItemChangeOf(invocation, 3)).Refusal);
}

VerbAdmission InventoryOperations::Drop(const VerbInvocation& invocation)
{
    EntityId entry;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, entry))
        return VerbAdmission::InvalidArguments;
    AuthoredVectorValue at;
    Transform3f where = Transform3f::Identity();
    if (invocation.Arguments->TryGetVector(1, at))
        where.Position = Vec3d(at.Components[0], at.Components[1], at.Components[2]);
    const EntityId holder = ItemHolder(*Entities, entry);
    const StoragePartitionId partition =
        Entities->IsAlive(holder) ? Entities->GetEntityPartition(holder) : StoragePartitionId::Default();
    return AdmissionOf(Inventory(*Entities).Drop(entry, where, partition, ItemChangeOf(invocation, 2)).Refusal);
}

VerbAdmission InventoryOperations::Use(const VerbInvocation& invocation)
{
    EntityId entry;
    std::optional<EntityId> user;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, entry)
        || !ReadAuthoredArgument(*invocation.Arguments, 1, user))
        return VerbAdmission::InvalidArguments;
    return AdmissionOf(Inventory(*Entities).Use(user.value_or(ItemHolder(*Entities, entry)), entry).Refusal);
}

AuthoredQueryBindingToken BindInventoryQuery(AuthoredQueryDispatcher& dispatcher, const World& world)
{
    return dispatcher.Bind<&CountUnits>(dispatcher.Registry().Find(kInventoryCountQuery), world);
}
