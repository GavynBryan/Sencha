#pragma once

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <inventory/Inventory.h>

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
