#pragma once

#include <anim/AnimPredicate.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Selector (`animation.selector`)
//
// An ordered rule list that maps a fact snapshot and the request set to one
// behavior on a layer. First match by priority, with one concession to memory:
// a rule's own stay predicate, which is the only place hysteresis is authored.
//
// A rule's result is a behavior, a nested selector (delegation, flattened when
// the rig binds), a named extension point a rig binds a selector to by data,
// or the layer's weight. Weight rules are a second first-match list over the
// same facts and requests: the first whose enter passes weights the layer, and
// none passing leaves the rig's constant. They carry no stay, hold or
// cooldown, so a weight is a function of this tick's inputs alone.
// There is no result that names content and no predicate operand that names a
// rule or behavior; both are absent from the format rather than rejected.
//=============================================================================

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
    // A label for the debugger and the editor; rules are identified by
    // position, not by name.
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
    // A weight rule's weight: a constant in [0, 1], or a float fact read each
    // tick and clamped to it, which is how gameplay fades a layer.
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
