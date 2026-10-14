#pragma once

#include <abilities/AbilityDeclaration.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>
#include <effects/EffectDefinition.h>

#include <string>
#include <string_view>
#include <vector>

class AttributeRegistry;
class GameplayTagRegistry;

// An effect authored as content. The compiled form names attributes and tags;
// declaring it into a World resolves them (docs/gameplay/abilitykit.md, "Effects").

inline constexpr std::string_view kEffectDefinitionType = "effect.definition";

struct EffectDeclaration
{
    struct Modifier
    {
        std::string Attribute;
        ModifierOp Op = ModifierOp::Add;
        float Magnitude = 0.0f;
    };

    EffectDuration Duration = EffectDuration::Instant;
    float Seconds = 0.0f;
    float Period = 0.0f;
    std::vector<Modifier> Modifiers;
    std::vector<std::string> GrantedTags;
    // Held by the target for as long as the effect lasts.
    std::vector<AbilityGrantDeclaration> GrantedAbilities;
};

void RegisterEffectDefinitionData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// The definition `declaration` means in a World with these registries, or false
// with one message per name neither registry knows.
[[nodiscard]] bool ResolveEffectDeclaration(const EffectDeclaration& declaration,
                                            const AttributeRegistry& attributes,
                                            const GameplayTagRegistry& tags,
                                            EffectDefinition& out,
                                            std::vector<std::string>& errors);
