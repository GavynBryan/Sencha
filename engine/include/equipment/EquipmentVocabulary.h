#pragma once

#include <abilities/AbilityCostProviders.h>
#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <equipment/Equipment.h>
#include <equipment/EquipmentComponents.h>
#include <gameplay_tags/GameplayTagId.h>
#include <inventory/InventoryComponents.h>

#include <optional>
#include <string_view>
#include <vector>

class FixedSimulationLoop;
class World;

inline constexpr std::string_view kEquipmentSlotCostProvider = "equipment.slot";

void DeclareEquipmentVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries);

class EquipmentOperations
{
public:
    EquipmentOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    SENCHA_VERB("equipment.equip")
    SENCHA_LABEL("Equip")
    SENCHA_CATEGORY("Equipment")
    SENCHA_DESCRIPTION("Wears an entry in the slots its item occupies.")
    VerbRefusal<EquipmentRefusal> Equip(
        const VerbInvocation& invocation,
        SENCHA_TARGET("wearer", EquipmentState) SENCHA_LABEL("Wearer") SENCHA_DESCRIPTION("Whose equipment.")
            EntityId wearer,
        SENCHA_TARGET("entry", ItemEntry) SENCHA_LABEL("Entry") SENCHA_DESCRIPTION("What is worn.") EntityId entry,
        SENCHA_ARG("slot") SENCHA_LABEL("Slot") SENCHA_DESCRIPTION("Where the first slot it occupies goes.")
            std::optional<std::string_view> slot = std::nullopt);

    SENCHA_VERB("equipment.unequip")
    SENCHA_LABEL("Unequip")
    SENCHA_CATEGORY("Equipment")
    SENCHA_DESCRIPTION("Takes off what a slot holds.")
    VerbRefusal<EquipmentRefusal> Unequip(
        const VerbInvocation& invocation,
        SENCHA_TARGET("wearer", EquipmentState) SENCHA_LABEL("Wearer") SENCHA_DESCRIPTION("Whose equipment.")
            EntityId wearer,
        SENCHA_ARG("slot") SENCHA_LABEL("Slot") SENCHA_DESCRIPTION("The slot emptied.") std::string_view slot,
        SENCHA_ARG("force") SENCHA_LABEL("Force")
            SENCHA_DESCRIPTION("What the World decides (a death's loot): no layer above can refuse it.")
                bool force = false);

    SENCHA_VERB("equipment.cycle")
    SENCHA_LABEL("Cycle equipment")
    SENCHA_CATEGORY("Equipment")
    SENCHA_DESCRIPTION("Wears the next entry the group's first slot accepts, in container order.")
    VerbRefusal<EquipmentRefusal> Cycle(
        const VerbInvocation& invocation,
        SENCHA_TARGET("wearer", EquipmentState) SENCHA_LABEL("Wearer") SENCHA_DESCRIPTION("Whose equipment.")
            EntityId wearer,
        SENCHA_ARG("group") SENCHA_LABEL("Group") SENCHA_DESCRIPTION("The slots cycled, as their group's name.")
            std::string_view group);

    SENCHA_QUERY("equipment.in_slot")
    SENCHA_LABEL("Worn in slot")
    SENCHA_CATEGORY("Equipment")
    SENCHA_DESCRIPTION("The entry a slot holds.")
    std::optional<EntityId> InSlot(
        SENCHA_TARGET("wearer", EquipmentState) SENCHA_LABEL("Wearer") SENCHA_DESCRIPTION("Whose equipment.")
            EntityId wearer,
        SENCHA_ARG("slot") SENCHA_LABEL("Slot") SENCHA_DESCRIPTION("The slot asked about.") std::string_view slot) const;

    SENCHA_QUERY("equipment.sources")
    SENCHA_LABEL("Worn with tag")
    SENCHA_CATEGORY("Equipment")
    SENCHA_DESCRIPTION("Every entry worn whose item carries a tag.")
    std::optional<std::vector<EntityId>> Sources(
        SENCHA_TARGET("wearer", EquipmentState) SENCHA_LABEL("Wearer") SENCHA_DESCRIPTION("Whose equipment.")
            EntityId wearer,
        SENCHA_ARG("tag") SENCHA_LABEL("Tag") GameplayTagId tag) const;

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    World* Entities;
    const FixedSimulationLoop* Clock;
};

[[nodiscard]] bool TranslateSlotCost(const World& world, const AbilityCostRow& row, std::vector<AuthoredValue>& arguments);
void DeclareSlotCostProvider(World& world);

#if !defined(SENCHA_CODEGEN)
#  include <equipment/EquipmentVocabulary.sencha.h>
#endif
