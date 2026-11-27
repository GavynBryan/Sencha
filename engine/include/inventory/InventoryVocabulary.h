#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredValueTraits.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <gameplay_tags/GameplayTagId.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/ItemDefinitionData.h>

#include <cstdint>
#include <math/Vec.h>
#include <optional>
#include <string_view>
#include <vector>

class FixedSimulationLoop;
class World;

void DeclareInventoryVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries);

[[nodiscard]] constexpr ItemChangeClass ItemChangeOf(bool forced)
{
    return forced ? ItemChangeClass::Forced : ItemChangeClass::Requested;
}

class InventoryOperations
{
public:
    InventoryOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    SENCHA_VERB("inventory.take")
    SENCHA_LABEL("Take item")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("Takes a pickup into a container; the pickup goes.")
    SENCHA_RESULT("entry", ItemEntry)
    VerbResult<std::optional<EntityId>, InventoryRefusal> Take(
        const VerbInvocation& invocation,
        SENCHA_TARGET("pickup", ItemPickup) SENCHA_LABEL("Pickup") SENCHA_DESCRIPTION("What is taken.") EntityId pickup,
        SENCHA_TARGET("into", ItemContainer) SENCHA_LABEL("Into") SENCHA_DESCRIPTION("The container it goes into.")
            EntityId into);

    SENCHA_VERB("inventory.give")
    SENCHA_LABEL("Give item")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("Puts units of an item into a container, all or none of them.")
    SENCHA_RESULT("entry", ItemEntry)
    VerbResult<std::optional<EntityId>, InventoryRefusal> Give(
        const VerbInvocation& invocation,
        SENCHA_TARGET("into", ItemContainer) SENCHA_LABEL("Into") SENCHA_DESCRIPTION("The container it goes into.")
            EntityId into,
        SENCHA_ARG("definition") SENCHA_LABEL("Item") ItemId definition,
        SENCHA_ARG("count") SENCHA_LABEL("Count") SENCHA_DESCRIPTION("How many units.") SENCHA_MIN(1)
            std::uint32_t count = 1);

    SENCHA_VERB("inventory.transfer")
    SENCHA_LABEL("Move item")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("Moves an entry, or part of a stack, into a container.")
    VerbRefusal<InventoryRefusal> Transfer(
        const VerbInvocation& invocation,
        SENCHA_TARGET("entry", ItemEntry) SENCHA_LABEL("Entry") SENCHA_DESCRIPTION("What moves.") EntityId entry,
        SENCHA_TARGET("into", ItemContainer) SENCHA_LABEL("Into") SENCHA_DESCRIPTION("The container it goes into.")
            EntityId into,
        SENCHA_ARG("count") SENCHA_LABEL("Count") SENCHA_DESCRIPTION("How many units; zero moves all of it.")
            std::uint32_t count = 0,
        SENCHA_ARG("force") SENCHA_LABEL("Force")
            SENCHA_DESCRIPTION("What the World decides (a death's loot): no layer above can refuse it.")
                bool force = false);

    SENCHA_VERB("inventory.drop")
    SENCHA_LABEL("Drop item")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("Takes an entry out of its container into a pickup, in the zone where it lands.")
    VerbRefusal<InventoryRefusal> Drop(
        const VerbInvocation& invocation,
        SENCHA_TARGET("entry", ItemEntry) SENCHA_LABEL("Entry") SENCHA_DESCRIPTION("What is dropped.") EntityId entry,
        SENCHA_ARG("at") SENCHA_LABEL("At") SENCHA_DESCRIPTION("Where it lands.") std::optional<Vec3d> at = std::nullopt,
        SENCHA_ARG("force") SENCHA_LABEL("Force")
            SENCHA_DESCRIPTION("What the World decides (a death's loot): no layer above can refuse it.")
                bool force = false,
        SENCHA_ARG("keep") SENCHA_LABEL("Keep")
            SENCHA_DESCRIPTION("The pickup stays with its zone, across its unload and in saves, until it is taken.")
                bool keep = true);

    SENCHA_VERB("inventory.use")
    SENCHA_LABEL("Use item")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("Runs the item's use, with the entry as its source, in the next ability pass.")
    VerbRefusal<InventoryRefusal> Use(
        const VerbInvocation& invocation,
        SENCHA_TARGET("entry", ItemEntry) SENCHA_LABEL("Entry") SENCHA_DESCRIPTION("What is used.") EntityId entry,
        SENCHA_ARG("user") SENCHA_LABEL("User") SENCHA_DESCRIPTION("Who uses it; whoever holds it when absent.")
            std::optional<EntityId> user = std::nullopt);

    SENCHA_QUERY("inventory.count")
    SENCHA_LABEL("Item count")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("How many units a container holds, spent entries not counted.")
    std::optional<std::int64_t> Count(
        SENCHA_TARGET("container", ItemContainer) SENCHA_LABEL("Container")
            SENCHA_DESCRIPTION("Whose items are counted.") EntityId container,
        SENCHA_ARG("definition") SENCHA_LABEL("Item") SENCHA_DATA_ASSET(kItemDefinitionType)
            SENCHA_DESCRIPTION("Counts only this item.") std::optional<AuthoredDataRef> definition = std::nullopt,
        SENCHA_ARG("tag") SENCHA_LABEL("Tag") SENCHA_DESCRIPTION("Counts only items carrying this tag or one beneath it.")
            std::optional<GameplayTagId> tag = std::nullopt,
        SENCHA_ARG("within") SENCHA_LABEL("Within") SENCHA_DESCRIPTION("Counts what its entries hold too, however deep.")
            bool within = false) const;

    SENCHA_QUERY("inventory.entry")
    SENCHA_LABEL("Item entry")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("Searches a container, in its order, for the first unspent entry of an item or carrying a "
                       "tag, or the last with 'last'. Not the entry a give made: that is the give's result.")
    std::optional<EntityId> Entry(
        SENCHA_TARGET("container", ItemContainer) SENCHA_LABEL("Container")
            SENCHA_DESCRIPTION("Whose entries are searched.") EntityId container,
        SENCHA_ARG("definition") SENCHA_LABEL("Item") SENCHA_DATA_ASSET(kItemDefinitionType)
            SENCHA_DESCRIPTION("Only entries of this item.") std::optional<AuthoredDataRef> definition = std::nullopt,
        SENCHA_ARG("tag") SENCHA_LABEL("Tag") SENCHA_DESCRIPTION("Only items carrying this tag or one beneath it.")
            std::optional<GameplayTagId> tag = std::nullopt,
        SENCHA_ARG("within") SENCHA_LABEL("Within")
            SENCHA_DESCRIPTION("Searches what its entries hold too, however deep.") bool within = false,
        SENCHA_ARG("last") SENCHA_LABEL("Last")
            SENCHA_DESCRIPTION("The last match in the container's order instead of the first.") bool last = false) const;

    SENCHA_QUERY("inventory.property")
    SENCHA_LABEL("Item property")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("The number an entry's item states for a property; unavailable when it states none, or a "
                       "value that is not a number.")
    std::optional<double> Property(
        SENCHA_TARGET("entry", ItemEntry) SENCHA_LABEL("Entry") SENCHA_DESCRIPTION("Whose item is read.")
            EntityId entry,
        SENCHA_ARG("property") SENCHA_LABEL("Property") SENCHA_DESCRIPTION("The game's name for the fact.")
            GameplayTagId property) const;

    SENCHA_QUERY("inventory.total")
    SENCHA_LABEL("Item property total")
    SENCHA_CATEGORY("Inventory")
    SENCHA_DESCRIPTION("Over a container's matching unspent entries, the sum of each entry's count times the "
                       "number its item states for a property; an item stating none adds nothing.")
    std::optional<double> Total(
        SENCHA_TARGET("container", ItemContainer) SENCHA_LABEL("Container")
            SENCHA_DESCRIPTION("Whose entries are summed.") EntityId container,
        SENCHA_ARG("property") SENCHA_LABEL("Property") SENCHA_DESCRIPTION("The game's name for the fact.")
            GameplayTagId property,
        SENCHA_ARG("definition") SENCHA_LABEL("Item") SENCHA_DATA_ASSET(kItemDefinitionType)
            SENCHA_DESCRIPTION("Only entries of this item.") std::optional<AuthoredDataRef> definition = std::nullopt,
        SENCHA_ARG("tag") SENCHA_LABEL("Tag") SENCHA_DESCRIPTION("Only items carrying this tag or one beneath it.")
            std::optional<GameplayTagId> tag = std::nullopt,
        SENCHA_ARG("within") SENCHA_LABEL("Within")
            SENCHA_DESCRIPTION("Sums what its entries hold too, however deep.") bool within = false) const;

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    World* Entities;
    const FixedSimulationLoop* Clock;
};

#if !defined(SENCHA_CODEGEN)
#  include <inventory/InventoryVocabulary.sencha.h>
#endif
