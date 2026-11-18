#include <equipment/EquipmentVocabulary.h>

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
    constexpr std::string_view kWearer = ComponentDefinition<EquipmentState>::Identity;
    constexpr std::string_view kEntry = ComponentDefinition<ItemEntry>::Identity;

    DataFieldSchema Wearer() { return MakeEntityArgument("wearer", "Wearer", kWearer, "Whose equipment."); }

    DataFieldSchema SlotArgument(std::string key, std::string label, std::string summary, bool required = true)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::String, std::move(key), std::move(label), std::move(summary));
        field.Required = required;
        return field;
    }

    VerbDefinition Verb(std::string_view name, std::string label, std::string description,
                        std::vector<DataFieldSchema> arguments)
    {
        return MakeGameplayVerb(name, std::move(label), std::move(description), "Equipment", std::move(arguments));
    }

    VerbAdmission AdmissionOf(EquipmentRefusal refusal)
    {
        return refusal == EquipmentRefusal::None ? VerbAdmission::Accepted : VerbAdmission::Refused;
    }

    EntityId WornIn(const World& world, EntityId wearer, std::string_view slot)
    {
        const EquipmentState* state =
            world.IsAlive(wearer) && world.IsRegistered<EquipmentState>() ? world.TryGet<EquipmentState>(wearer) : nullptr;
        const EquipmentLayout* layout = state != nullptr ? FindEquipmentLayout(world, state->Layout) : nullptr;
        const std::uint8_t index = layout != nullptr ? FindEquipmentSlot(*layout, slot) : kNoEquipmentSlot;
        if (index == kNoEquipmentSlot)
            return EntityId{};
        for (std::uint8_t i = 0; i < state->Count; ++i)
            if ((state->Records[i].Slots >> index) & 1u)
                return ResolvePersistableRef(world, state->Records[i].Source);
        return EntityId{};
    }

    AuthoredQueryStatus InSlot(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        EntityId wearer;
        std::string_view slot;
        if (arguments.size() < 2 || !arguments[0].TryGetEntity(wearer) || !arguments[1].TryGetString(slot))
            return AuthoredQueryStatus::InvalidArguments;
        const EntityId worn = WornIn(world, wearer, slot);
        if (!worn.IsValid())
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Entity(worn);
        return AuthoredQueryStatus::Value;
    }

    AuthoredQueryStatus Sources(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        EntityId wearer;
        GameplayTagId tag;
        if (arguments.size() < 2 || !arguments[0].TryGetEntity(wearer) || !arguments[1].TryGetTag(tag))
            return AuthoredQueryStatus::InvalidArguments;
        const EquipmentState* state =
            world.IsAlive(wearer) && world.IsRegistered<EquipmentState>() ? world.TryGet<EquipmentState>(wearer) : nullptr;
        if (state == nullptr)
            return AuthoredQueryStatus::Unavailable;
        std::vector<AuthoredValue> worn;
        for (std::uint8_t i = 0; i < state->Count; ++i)
        {
            const ItemDefinition* item = FindItemDefinition(world, state->Records[i].Definition);
            const EntityId source = ResolvePersistableRef(world, state->Records[i].Source);
            if (item != nullptr && source.IsValid() && ItemCarries(world, *item, tag))
                worn.push_back(AuthoredValue::Entity(source));
        }
        result = AuthoredValue::Array(std::move(worn));
        return AuthoredQueryStatus::Value;
    }
}

void DeclareEquipmentVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries)
{
    (void)verbs.Declare(Verb(kEquipmentEquipVerb, "Equip", "Wears an entry in the slots its item occupies.",
                             { Wearer(), MakeEntityArgument("entry", "Entry", kEntry, "What is worn."),
                               SlotArgument("slot", "Slot", "Where the first slot it occupies goes.", false) }));
    (void)verbs.Declare(Verb(kEquipmentUnequipVerb, "Unequip", "Takes off what a slot holds.",
                             { Wearer(), SlotArgument("slot", "Slot", "The slot emptied."), ItemChangeArgument() }));
    (void)verbs.Declare(Verb(kEquipmentCycleVerb, "Cycle equipment",
                             "Wears the next entry the group's first slot accepts, in container order.",
                             { Wearer(), SlotArgument("group", "Group", "The slots cycled, as their group's name.") }));

    AuthoredQueryDefinition inSlot;
    inSlot.Name = std::string(kEquipmentInSlotQuery);
    inSlot.DisplayName = "Worn in slot";
    inSlot.Description = "The entry a slot holds.";
    inSlot.Category = "Equipment";
    inSlot.Arguments.Children = { Wearer(), SlotArgument("slot", "Slot", "The slot asked about.") };
    inSlot.Result.Kind = DataFieldKind::Entity;
    (void)queries.Declare(std::move(inSlot));

    AuthoredQueryDefinition sources;
    sources.Name = std::string(kEquipmentSourcesQuery);
    sources.DisplayName = "Worn with tag";
    sources.Description = "Every entry worn whose item carries a tag.";
    sources.Category = "Equipment";
    sources.Arguments.Children = { Wearer(), MakeDataField(DataFieldKind::GameplayTag, "tag", "Tag") };
    sources.Result = MakeDataField(DataFieldKind::Array, "entries", "Entries");
    sources.Result.Children = { MakeDataField(DataFieldKind::Entity, "entry", "Entry") };
    (void)queries.Declare(std::move(sources));
}

VerbAdmission EquipmentOperations::Equip(const VerbInvocation& invocation)
{
    EntityId wearer;
    EntityId entry;
    std::string_view slot;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, wearer)
        || !invocation.Arguments->TryGetEntity(1, entry))
        return VerbAdmission::InvalidArguments;
    (void)invocation.Arguments->TryGetString(2, slot);
    return AdmissionOf(Equipment(*Entities, Clock->TimeOfTick(invocation.Tick)).Equip(wearer, entry, slot).Refusal);
}

VerbAdmission EquipmentOperations::Unequip(const VerbInvocation& invocation)
{
    EntityId wearer;
    std::string_view slot;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, wearer)
        || !invocation.Arguments->TryGetString(1, slot))
        return VerbAdmission::InvalidArguments;
    return AdmissionOf(
        Equipment(*Entities, Clock->TimeOfTick(invocation.Tick)).Unequip(wearer, slot, ItemChangeOf(invocation, 2)).Refusal);
}

VerbAdmission EquipmentOperations::Cycle(const VerbInvocation& invocation)
{
    EntityId wearer;
    std::string_view group;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, wearer)
        || !invocation.Arguments->TryGetString(1, group))
        return VerbAdmission::InvalidArguments;
    return AdmissionOf(Equipment(*Entities, Clock->TimeOfTick(invocation.Tick)).Cycle(wearer, group).Refusal);
}

std::vector<AuthoredQueryBindingToken> BindEquipmentQueries(AuthoredQueryDispatcher& dispatcher, const World& world)
{
    const AuthoredQueryRegistry& registry = dispatcher.Registry();
    std::vector<AuthoredQueryBindingToken> tokens;
    tokens.push_back(dispatcher.Bind<&InSlot>(registry.Find(kEquipmentInSlotQuery), world));
    tokens.push_back(dispatcher.Bind<&Sources>(registry.Find(kEquipmentSourcesQuery), world));
    return tokens;
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
    // inventory.item's arguments: the entry, no container, the count.
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
