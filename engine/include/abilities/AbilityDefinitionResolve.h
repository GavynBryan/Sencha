#pragma once

#include <abilities/AbilityDeclaration.h>
#include <abilities/AbilityDefinition.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbBindingCompiler.h>

#include <string_view>
#include <vector>

class AttributeRegistry;
class EffectRegistry;
class GameplayTagRegistry;
class World;

struct AbilityResolveEnvironment
{
    const GameplayTagRegistry* Tags = nullptr;
    const AttributeRegistry* Attributes = nullptr;
    const EffectRegistry* Effects = nullptr;
    const AuthoredQueryRegistry* Queries = nullptr;
    VerbBindingEnvironment Verbs;
    // Where component identities resolve to this World's component ids.
    const World* Components = nullptr;
};

[[nodiscard]] AbilityResolveEnvironment MakeAbilityResolveEnvironment(const World& world);

// The definition `declaration` means in this World: every name resolved, every
// operation's arguments checked against its contract, and the guards an argument's
// declared recipient needs. `name` prefixes binding keys. True when nothing is an
// error.
[[nodiscard]] bool ResolveAbilityDeclaration(const AbilityDeclaration& declaration,
                                             std::string_view name,
                                             const AbilityResolveEnvironment& environment,
                                             AbilityDefinition& out,
                                             std::vector<AbilityDiagnostic>& diagnostics);
