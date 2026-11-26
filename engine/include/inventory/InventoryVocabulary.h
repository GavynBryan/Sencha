#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredValueTraits.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <gameplay_tags/GameplayTagId.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/ItemDefinitionData.h>

#include <optional>
#include <string_view>
#include <vector>

class FixedSimulationLoop;
class World;

inline constexpr std::string_view kInventoryTakeVerb = "inventory.take";
inline constexpr std::string_view kInventoryGiveVerb = "inventory.give";
inline constexpr std::string_view kInventoryTransferVerb = "inventory.transfer";
inline constexpr std::string_view kInventoryDropVerb = "inventory.drop";
inline constexpr std::string_view kInventoryUseVerb = "inventory.use";
inline constexpr std::string_view kInventoryCountQuery = "inventory.count";

void DeclareInventoryVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries);

[[nodiscard]] DataFieldSchema ItemChangeArgument();
[[nodiscard]] ItemChangeClass ItemChangeOf(const VerbInvocation& invocation, std::size_t slot);

class InventoryOperations
{
public:
    InventoryOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    VerbAnswer Take(const VerbInvocation& invocation);
    VerbAnswer Give(const VerbInvocation& invocation);
    VerbAnswer Transfer(const VerbInvocation& invocation);
    VerbAnswer Drop(const VerbInvocation& invocation);
    VerbAnswer Use(const VerbInvocation& invocation);

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

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    World* Entities;
    const FixedSimulationLoop* Clock;
};

inline VerbAnswer InvokeInventoryTake(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Take(invocation);
}

inline VerbAnswer InvokeInventoryGive(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Give(invocation);
}

inline VerbAnswer InvokeInventoryTransfer(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Transfer(invocation);
}

inline VerbAnswer InvokeInventoryDrop(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Drop(invocation);
}

inline VerbAnswer InvokeInventoryUse(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Use(invocation);
}

[[nodiscard]] AuthoredQueryBindingToken BindInventoryQuery(AuthoredQueryDispatcher& dispatcher, const World& world);

#if !defined(SENCHA_CODEGEN)
#  include <inventory/InventoryVocabulary.sencha.h>
#endif
