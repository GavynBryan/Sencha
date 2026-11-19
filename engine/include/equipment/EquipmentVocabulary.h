#pragma once

#include <abilities/AbilityCostProviders.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <equipment/Equipment.h>

#include <string_view>
#include <vector>

class FixedSimulationLoop;
class Logger;
class World;

inline constexpr std::string_view kEquipmentEquipVerb = "equipment.equip";
inline constexpr std::string_view kEquipmentUnequipVerb = "equipment.unequip";
inline constexpr std::string_view kEquipmentCycleVerb = "equipment.cycle";
inline constexpr std::string_view kEquipmentInSlotQuery = "equipment.in_slot";
inline constexpr std::string_view kEquipmentSourcesQuery = "equipment.sources";
inline constexpr std::string_view kEquipmentSlotCostProvider = "equipment.slot";

void DeclareEquipmentVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries);

class EquipmentOperations
{
public:
    EquipmentOperations(World& world, const FixedSimulationLoop& clock, const Logger& log)
        : Entities(&world)
        , Clock(&clock)
        , Log(&log)
    {
    }

    VerbAdmission Equip(const VerbInvocation& invocation);
    VerbAdmission Unequip(const VerbInvocation& invocation);
    VerbAdmission Cycle(const VerbInvocation& invocation);

private:
    VerbAdmission Answer(std::string_view verb, EquipmentRefusal refusal) const;

    World* Entities;
    const FixedSimulationLoop* Clock;
    const Logger* Log;
};

inline VerbAdmission InvokeEquipmentEquip(EquipmentOperations& operations, const VerbInvocation& invocation)
{
    return operations.Equip(invocation);
}

inline VerbAdmission InvokeEquipmentUnequip(EquipmentOperations& operations, const VerbInvocation& invocation)
{
    return operations.Unequip(invocation);
}

inline VerbAdmission InvokeEquipmentCycle(EquipmentOperations& operations, const VerbInvocation& invocation)
{
    return operations.Cycle(invocation);
}

[[nodiscard]] std::vector<AuthoredQueryBindingToken> BindEquipmentQueries(AuthoredQueryDispatcher& dispatcher,
                                                                          const World& world);

[[nodiscard]] bool TranslateSlotCost(const World& world, const AbilityCostRow& row, std::vector<AuthoredValue>& arguments);
void DeclareSlotCostProvider(World& world);
