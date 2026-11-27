#include <inventory/InventoryVocabulary.h>

#include <authored/AuthoredApi.h>
#include <ecs/World.h>
#include <inventory/InventoryComponents.h>
#include <inventory/ItemDefinitionData.h>
#include <time/SimClock.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePlacement.h>

#include <optional>

namespace
{
    VerbResult<std::optional<EntityId>, InventoryRefusal> Answered(const InventoryResult& result)
    {
        if (result.Refusal != InventoryRefusal::None)
            return result.Refusal;
        return result.Entry.IsValid() ? std::optional<EntityId>(result.Entry) : std::nullopt;
    }

    std::optional<ItemFilter> FilterOf(const World& world, std::optional<AuthoredDataRef> definition,
                                       std::optional<GameplayTagId> tag, bool within)
    {
        ItemFilter filter{ .Tag = tag.value_or(GameplayTagId{}), .Within = within };
        if (!definition.has_value())
            return filter;
        filter.Definition = ItemId{ NamedIdField<ItemId>::Vocabulary().Find(world, definition->Reference->Path) };
        return filter.Definition.IsValid() ? std::optional<ItemFilter>(filter) : std::nullopt;
    }
}

void DeclareInventoryVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries)
{
    DeclareAuthoredApi<InventoryOperations>(&verbs, &queries);
}

VerbResult<std::optional<EntityId>, InventoryRefusal> InventoryOperations::Take(const VerbInvocation& invocation,
                                                                                  EntityId pickup, EntityId into)
{
    return Answered(Inventory(*Entities, Clock->TimeOfTick(invocation.Tick)).Take(pickup, into));
}

VerbResult<std::optional<EntityId>, InventoryRefusal> InventoryOperations::Give(const VerbInvocation& invocation,
                                                                                  EntityId into, ItemId definition,
                                                                                  std::uint32_t count)
{
    return Answered(Inventory(*Entities, Clock->TimeOfTick(invocation.Tick)).Give(into, definition, count));
}

VerbRefusal<InventoryRefusal> InventoryOperations::Transfer(const VerbInvocation& invocation, EntityId entry,
                                                            EntityId into, std::uint32_t count, bool force)
{
    return Inventory(*Entities, Clock->TimeOfTick(invocation.Tick))
        .Transfer(entry, into, count, ItemChangeOf(force))
        .Refusal;
}

VerbRefusal<InventoryRefusal> InventoryOperations::Drop(const VerbInvocation& invocation, EntityId entry,
                                                        std::optional<Vec3d> at, bool force, bool keep)
{
    Transform3f where = Transform3f::Identity();
    if (at.has_value())
        where.Position = *at;
    const EntityId holder = ItemHolder(*Entities, entry);
    StoragePartitionId partition =
        Entities->IsAlive(holder) ? Entities->GetEntityPartition(holder) : StoragePartitionId::Default();
    if (const ZonePlacement* zones = Entities->TryGetResource<ZonePlacement>(); zones != nullptr && zones->PartitionAt)
        if (const std::optional<StoragePartitionId> landed = zones->PartitionAt(where.Position))
            partition = *landed;
    const InventoryResult dropped = Inventory(*Entities, Clock->TimeOfTick(invocation.Tick))
                                        .Drop(entry, where, partition, ItemChangeOf(force));
    if (dropped.Refusal == InventoryRefusal::None && keep)
        (void)KeepWithZone(*Entities, dropped.Entry);
    return dropped.Refusal;
}

VerbRefusal<InventoryRefusal> InventoryOperations::Use(const VerbInvocation& invocation, EntityId entry,
                                                       std::optional<EntityId> user)
{
    return Inventory(*Entities, Clock->TimeOfTick(invocation.Tick))
        .Use(user.value_or(ItemHolder(*Entities, entry)), entry)
        .Refusal;
}

std::optional<std::int64_t> InventoryOperations::Count(EntityId container, std::optional<AuthoredDataRef> definition,
                                                       std::optional<GameplayTagId> tag, bool within) const
{
    if (!Entities->IsAlive(container))
        return std::nullopt;
    const std::optional<ItemFilter> filter = FilterOf(*Entities, definition, tag, within);
    return filter.has_value() ? static_cast<std::int64_t>(CountItemUnits(*Entities, container, *filter)) : 0;
}

std::optional<EntityId> InventoryOperations::Entry(EntityId container, std::optional<AuthoredDataRef> definition,
                                                   std::optional<GameplayTagId> tag, bool within, bool last) const
{
    const std::optional<ItemFilter> filter = FilterOf(*Entities, definition, tag, within);
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
    const std::optional<ItemFilter> filter = FilterOf(*Entities, definition, tag, within);
    return filter.has_value() ? ItemPropertyTotal(*Entities, container, property, *filter) : 0.0;
}
