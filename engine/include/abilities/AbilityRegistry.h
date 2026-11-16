#pragma once

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityId.h>
#include <core/identity/NamedDefinitionRegistry.h>

// One World's abilities, by the asset path that declares each.
class AbilityRegistry final : public NamedDefinitionRegistry<AbilityId, AbilityDefinition>
{
};
