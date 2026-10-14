#include <abilities/AbilityTrace.h>

std::string_view AbilityRefusalName(AbilityRefusal refusal)
{
    switch (refusal)
    {
    case AbilityRefusal::None: return "none";
    case AbilityRefusal::NotGranted: return "not granted";
    case AbilityRefusal::Undefined: return "undefined";
    case AbilityRefusal::ActivationFailed: return "activation failed";
    case AbilityRefusal::Instances: return "at its instance limit";
    case AbilityRefusal::Capacity: return "no room";
    case AbilityRefusal::CostFailed: return "cannot pay";
    }
    return "unknown";
}
