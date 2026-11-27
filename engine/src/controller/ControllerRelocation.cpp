#include <controller/ControllerComponentRegistration.h>

#include <world/relocation/RelocationRules.h>

namespace
{
    // Pending look input is a displacement from this aim, not a heading, so it needs no turn.
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
