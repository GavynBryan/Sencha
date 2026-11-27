#include <equipment/EquipmentVocabulary.h>

#include <authored/AuthoredApi.h>
#include <authored/AuthoredValue.h>
#include <ecs/World.h>
#include <equipment/Equipment.h>
#include <equipment/EquipmentComponents.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryVocabulary.h>
#include <inventory/ItemCostProvider.h>
#include <time/SimClock.h>

#include <string>
#include <utility>

namespace
{
    EntityId WornIn(const World& world, EntityId wearer, std::string_view slot)
    {
        const EquipmentState* state =
            world.IsAlive(wearer) && world.IsRegistered<EquipmentState>() ? world.TryGet<EquipmentState>(wearer) : nullptr;
        const EquipmentLayout* layout = state != nullptr ? FindEquipmentLayout(world, state->Layout) : nullptr;
        const std::uint8_t index = layout != nullptr ? FindEquipmentSlot(*layout, slot) : kNoEquipmentSlot;
        if (index == kNoEquipmentSlot)
            return EntityId{};
        const std::span<const EquipmentRecord> records = EquipmentRecordsOf(world, *state);
        const std::size_t holding = FindEquipmentRecordHolding(records, index);
        return holding != kNoEquipmentRecord ? ResolvePersistableRef(world, records[holding].Source) : EntityId{};
    }
}

void DeclareEquipmentVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries)
{
    DeclareAuthoredApi<EquipmentOperations>(&verbs, &queries);
}

VerbRefusal<EquipmentRefusal> EquipmentOperations::Equip(const VerbInvocation& invocation, EntityId wearer,
                                                         EntityId entry, std::optional<std::string_view> slot)
{
    return Equipment(*Entities, Clock->TimeOfTick(invocation.Tick)).Equip(wearer, entry, slot.value_or("")).Refusal;
}

VerbRefusal<EquipmentRefusal> EquipmentOperations::Unequip(const VerbInvocation& invocation, EntityId wearer,
                                                           std::string_view slot, bool force)
{
    return Equipment(*Entities, Clock->TimeOfTick(invocation.Tick)).Unequip(wearer, slot, ItemChangeOf(force)).Refusal;
}

VerbRefusal<EquipmentRefusal> EquipmentOperations::Cycle(const VerbInvocation& invocation, EntityId wearer,
                                                         std::string_view group)
{
    return Equipment(*Entities, Clock->TimeOfTick(invocation.Tick)).Cycle(wearer, group).Refusal;
}

std::optional<EntityId> EquipmentOperations::InSlot(EntityId wearer, std::string_view slot) const
{
    const EntityId worn = WornIn(*Entities, wearer, slot);
    return worn.IsValid() ? std::optional<EntityId>(worn) : std::nullopt;
}

std::optional<std::vector<EntityId>> EquipmentOperations::Sources(EntityId wearer, GameplayTagId tag) const
{
    const EquipmentState* state = Entities->IsAlive(wearer) && Entities->IsRegistered<EquipmentState>()
        ? Entities->TryGet<EquipmentState>(wearer)
        : nullptr;
    if (state == nullptr)
        return std::nullopt;
    std::vector<EntityId> worn;
    for (const EquipmentRecord& record : EquipmentRecordsOf(*Entities, *state))
    {
        const ItemDefinition* item = FindItemDefinition(*Entities, record.Definition);
        const EntityId source = ResolvePersistableRef(*Entities, record.Source);
        if (item != nullptr && source.IsValid() && ItemCarries(*Entities, *item, tag))
            worn.push_back(source);
    }
    return worn;
}

bool TranslateSlotCost(const World& world, const AbilityCostRow& row, std::vector<AuthoredValue>& arguments)
{
    std::string_view slot;
    std::int64_t count = 1;
    if (row.Arguments.empty() || !row.Arguments[0].TryGetString(slot))
        return false;
    if (row.Arguments.size() > 1)
        (void)row.Arguments[1].TryGetInt(count);
    const EntityId worn = WornIn(world, row.Actor, slot);
    if (!worn.IsValid())
        return false;
    arguments.push_back(AuthoredValue::Entity(worn));
    arguments.emplace_back();
    arguments.push_back(AuthoredValue::Int(count));
    return true;
}

void DeclareSlotCostProvider(World& world)
{
    AbilityCostProviderCatalog* catalog = world.TryGetResource<AbilityCostProviderCatalog>();
    if (catalog == nullptr || catalog->Find(kEquipmentSlotCostProvider).IsValid())
        return;
    AbilityCostProviderDefinition slot;
    slot.Name = std::string(kEquipmentSlotCostProvider);
    slot.DisplayName = "Worn item";
    slot.Description = "Units spent from the entry a slot holds; a slot holding nothing cannot pay.";
    DataFieldSchema key = MakeDataField(DataFieldKind::String, "slot", "Slot");
    DataFieldSchema count = MakeDataField(DataFieldKind::Int, "count", "Count");
    count.Default = std::int64_t{ 1 };
    count.Required = false;
    slot.Arguments.Children = { std::move(key), std::move(count) };
    slot.SpendsThrough = std::string(kItemCostProvider);
    slot.Translate = &TranslateSlotCost;
    AbilityCostProviderRegistrationScope scope(*catalog, "equipment");
    (void)scope.Declare(std::move(slot));
    (void)scope.Commit();
}
