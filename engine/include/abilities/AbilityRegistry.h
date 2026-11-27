#pragma once

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityId.h>
#include <core/identity/NamedDefinitionRegistry.h>

class AbilityRegistry final : public NamedDefinitionRegistry<AbilityId, AbilityDefinition>
{
};
