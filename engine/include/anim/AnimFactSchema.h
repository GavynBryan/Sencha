#pragma once

#include <anim/AnimTypes.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAnimFactSchemaType = "animation.fact_schema";

struct AnimFactSlotDecl
{
    std::string Name;
    AnimFactKind Kind = AnimFactKind::Float;
    // Exists only on the rendering machine (camera facing, a cosmetic seed). Other
    // facts derive from replicated state, so the snapshot is reconstructible.
    bool Local = false;
};

// Closed. Temporal windows are capped at kAnimMaxDerivationWindowMs, and every op
// reads other facts only: never selection, flows, content or pose.
enum class AnimDerivationOp : std::uint8_t
{
    Edge,        // bool: the source changed in Direction within the last WindowMs
    TimeSince,   // float seconds since the source was last MatchValue, capped at WindowMs
    Hysteresis,  // bool: rises when the source reaches Enter, falls when it drops to Exit
    MinDuration, // bool: the source has held true for at least WindowMs
    Smooth,      // float: exponential smoothing with time constant WindowMs
    Compare,     // bool: source <op> Constant
    And,         // bool: every source true
    Or,          // bool: any source true
    Not,         // bool: the source false
};

enum class AnimCompareOp : std::uint8_t
{
    Lt,
    Le,
    Gt,
    Ge,
    Eq,
    Ne,
};

[[nodiscard]] std::string_view AnimDerivationOpName(AnimDerivationOp op);

// `not Grounded` is an operand rather than a separate fact: Airborne is
// MinDuration(not Grounded, 80).
struct AnimFactOperand
{
    std::string Fact;
    bool Negate = false;
};

struct AnimDerivedFactDecl
{
    std::string Name;
    AnimDerivationOp Op = AnimDerivationOp::Edge;

    // One source for every op but And and Or, which take two or more.
    std::vector<AnimFactOperand> Sources;

    bool Rising = true;         // Edge
    float WindowMs = 0.0f;      // Edge hold, TimeSince cap, MinDuration, Smooth tau
    bool MatchValue = false;    // TimeSince
    float Enter = 0.0f;         // Hysteresis
    float Exit = 0.0f;          // Hysteresis
    AnimCompareOp Compare = AnimCompareOp::Gt;
    float Constant = 0.0f;      // Compare

    [[nodiscard]] AnimFactKind ResultKind() const;
    // Temporal ops contribute their window to the schema's observation horizon.
    [[nodiscard]] bool IsTemporal() const;
};

struct AnimFactSchema
{
    // The schema this one extends ("asset://..."), or empty.
    std::string Extends;
    std::vector<AnimFactSlotDecl> Slots;
    std::vector<AnimDerivedFactDecl> Derived;
};

// The engine's four character facts, which a game's schema extends.
inline constexpr std::string_view kAnimEngineFactSchemaPath = "asset://animation/engine.facts.sdata";

void RegisterAnimFactSchema(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
void UnregisterAnimFactSchema(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// An identifier, optionally dotted.
[[nodiscard]] bool IsValidAnimFactName(std::string_view name);
