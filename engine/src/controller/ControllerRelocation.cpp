#include <controller/ControllerComponentRegistration.h>

#include <world/relocation/RelocationRules.h>

namespace
{
    void TurnYaw(LookOrientation& look, const RelocationFrame& frame)
    {
        look.Yaw += frame.Yaw;
    }
}

void RegisterControllerRelocation(World& world)
{
    SetRelocationRule<LookOrientation>(world, RelocationRuleKind::MapsWhenUpright,
                                       &RelocateValue<LookOrientation, &TurnYaw>);
}
