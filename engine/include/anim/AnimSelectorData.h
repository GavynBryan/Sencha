#pragma once

#include <anim/AnimPredicate.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAnimSelectorType = "animation.selector";

// How deep delegation may nest, counting the selector a layer names as one.
inline constexpr std::size_t kAnimMaxSelectorDepth = 4;

enum class AnimRuleResultKind : std::uint8_t
{
    Behavior,
    Delegate,
    Extension,
    Weight,
};

struct AnimSelectorRuleDecl
{
    // Keeps the rule's state across a reload that reorders rules; an unnamed rule is
    // identified by position.
    std::string Name;
    std::int32_t Priority = 0;
    AnimPredicateDecl Enter;
    // Absent means the rule stays while its enter still passes.
    bool HasStay = false;
    AnimPredicateDecl Stay;

    AnimRuleResultKind Result = AnimRuleResultKind::Behavior;
    std::string Behavior;
    std::string Delegate;
    std::string Extension;
    // A constant in [0, 1], or a float fact read each tick and clamped to it.
    float Weight = 1.0f;
    std::string WeightFact;

    float HoldMinMs = 0.0f;
    float CooldownMs = 0.0f;
};

struct AnimSelectorData
{
    std::vector<AnimSelectorRuleDecl> Rules;
};

void RegisterAnimSelectorData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
