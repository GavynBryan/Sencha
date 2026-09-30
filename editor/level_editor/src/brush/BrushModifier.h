#pragma once

#include "ArrayModifier.h"
#include "MirrorModifier.h"

#include <span>
#include <variant>
#include <vector>

// One entry of a brush's modifier stack: an enable flag plus the parameters of
// exactly one modifier kind. The stack is ordered authoring data of the brush
// record and evaluates top to bottom (BrushEvaluation.h).
//
// A modifier automates a relationship, not merely a transform: Array repeats
// what the stack has produced so far along a local axis with a gap, Mirror
// reflects it across the brush's own origin. The general math is underneath;
// the parameters name the intent.
struct BrushModifier
{
    bool Enabled = true;
    std::variant<MirrorModifier, ArrayModifier> Params;
};

using BrushModifierStack = std::vector<BrushModifier>;

// What the kernel and serializer know about a kind: its identity on disk and
// its authoring default. Indexed like the variant. Display labels are editor
// chrome and live with the inspector. Adding a kind is a variant alternative
// and one row here.
struct BrushModifierKindInfo
{
    const char* JsonKey;
    BrushModifier (*MakeDefault)();
};
[[nodiscard]] std::span<const BrushModifierKindInfo> BrushModifierKinds();
[[nodiscard]] const BrushModifierKindInfo& BrushModifierKindOf(const BrushModifier& modifier);

// Keeps an independently authored plane in place when the brush's local frame
// moves: after the mesh vertices are translated by `delta` (a re-origin), a
// Custom mirror plane shifts by the same delta. Everything derived from the
// origin or the geometry deliberately does NOT shift: an Origin mirror is
// meant to follow the origin, a BoundsCenter one follows the vertices by
// itself, and Array placements are directions.
void RebaseBrushModifiers(BrushModifierStack& stack, Vec3d delta);

// Which revision domains a stack's parameters feed. Topology folds every
// parameter of a modifier that mints meshes (a Mirror's plane changes the
// reflected mesh itself); Placement folds every parameter that moves pieces,
// which includes those and a placement-only modifier's own (an Array's count,
// spacing, axis). Each kind declares its own contribution here, so a new kind
// cannot leave a retained fact stale by omission.
struct BrushModifierSignatures
{
    std::uint64_t Topology = 0;
    std::uint64_t Placement = 0;
};
[[nodiscard]] BrushModifierSignatures BrushStackSignatures(const BrushModifierStack& stack);
