#pragma once

#include <anim/AnimTypes.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Fact schema (`animation.fact_schema`)
//
// The typed slots an animated entity's rules may read, and the facts derived
// from them. Open to games and mods -- anyone may declare a slot by name --
// while the derivation ops are closed, so rule evaluation stays a fixed
// program over fixed slots.
//
// A schema may extend another. A rig references one schema; the engine ships a
// core schema, and a game's schema extends it with the game's own slots. The
// chain merges when the rig binds into a World, base slots first, into one
// fixed layout; a name declared twice anywhere in the chain is a conflict, not
// an override.
//
// The compiled value holds names only. Slot indices and tag ids are World-local
// and belong to the bound layout (AnimRigBinding), never to this asset.
//=============================================================================

inline constexpr std::string_view kAnimFactSchemaType = "animation.fact_schema";

struct AnimFactSlotDecl
{
    std::string Name;
    AnimFactKind Kind = AnimFactKind::Float;
    // Exists only on the rendering machine: camera facing, quality tier, a
    // cosmetic variant seed. Everything else is a function of replicated
    // gameplay state, which is what makes the fact snapshot reconstructible.
    bool Local = false;
};

// The closed derivation set. Every temporal op has a window no longer than
// kAnimMaxDerivationWindowMs, and every op reads other facts and nothing else:
// no derivation can see selection, flows, content or pose.
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

// A reference to another fact, optionally negated: `not Grounded` is an operand,
// not a separate derived fact, which is how Airborne reads as
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
    // Whether the op keeps time, and so contributes its window to the schema's
    // observation horizon.
    [[nodiscard]] bool IsTemporal() const;
};

struct AnimFactSchema
{
    // The schema this one extends ("asset://..."), or empty.
    std::string Extends;
    std::vector<AnimFactSlotDecl> Slots;
    std::vector<AnimDerivedFactDecl> Derived;
};

// The engine's own schema: the four facts every character-like rig reads, and
// the one a game's schema extends.
inline constexpr std::string_view kAnimEngineFactSchemaPath = "asset://animation/engine.facts.sdata";

void RegisterAnimFactSchema(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
void UnregisterAnimFactSchema(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// A fact or intent name as content may spell one: an identifier, optionally
// dotted. Shared by every animation asset that names a fact.
[[nodiscard]] bool IsValidAnimFactName(std::string_view name);
