#include <logic/LogicRegistration.h>

#include <logic/decision/DecisionAgentSystem.h>
#include <world/relocation/RelocationRules.h>

namespace
{
    void ForgetPlace(void*, World& world, EntityId agent, const RelocationFrame&)
    {
        ForgetDecisionPlace(world, agent);
    }
}

void RegisterLogicRelocation(World& world)
{
    SetRelocationRule<DecisionAgent>(world, RelocationRuleKind::Maps, &ForgetPlace);
}
