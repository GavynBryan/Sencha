#pragma once

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <inventory/Inventory.h>

#include <string_view>
#include <vector>

class FixedSimulationLoop;
class Logger;
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
    InventoryOperations(World& world, const FixedSimulationLoop& clock, const Logger& log)
        : Entities(&world)
        , Clock(&clock)
        , Log(&log)
    {
    }

    VerbAdmission Take(const VerbInvocation& invocation);
    VerbAdmission Give(const VerbInvocation& invocation);
    VerbAdmission Transfer(const VerbInvocation& invocation);
    VerbAdmission Drop(const VerbInvocation& invocation);
    VerbAdmission Use(const VerbInvocation& invocation);

private:
    VerbAdmission Answer(std::string_view verb, InventoryRefusal refusal) const;

    World* Entities;
    const FixedSimulationLoop* Clock;
    const Logger* Log;
};

inline VerbAdmission InvokeInventoryTake(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Take(invocation);
}

inline VerbAdmission InvokeInventoryGive(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Give(invocation);
}

inline VerbAdmission InvokeInventoryTransfer(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Transfer(invocation);
}

inline VerbAdmission InvokeInventoryDrop(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Drop(invocation);
}

inline VerbAdmission InvokeInventoryUse(InventoryOperations& operations, const VerbInvocation& invocation)
{
    return operations.Use(invocation);
}

[[nodiscard]] AuthoredQueryBindingToken BindInventoryQuery(AuthoredQueryDispatcher& dispatcher, const World& world);
