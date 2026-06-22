#pragma once

#include "MeshElementKind.h"

#include "../selection/SelectableRef.h" // SelectableKind (already an in-layer dep)

#include <array>
#include <cstddef>

//=============================================================================
// MeshElementKindTraits — the single data home for the per-element-mode facts
// that used to be re-derived by scattered switches (cycle order, the mode<->
// selectable mapping, the display label). Mirrors viewport/ViewportOrientation:
// one array is the authority, accessors index into it.
//
// Layer-agnostic facts ONLY — nothing that would pull viewport/UI/render into
// meshedit (the meshedit_dependency_directions ctest). The viewport pick-mode
// and the UI icon are each a small table in their OWN layer, keyed by
// MeshElementKind, not switches.
//
// Adding a MeshElementKind = add the enum value + one row here.
//=============================================================================

inline constexpr std::size_t MeshElementKindCount = 4;

struct MeshElementKindTraits
{
    MeshElementKind Kind       = MeshElementKind::Object;
    SelectableKind  Selectable = SelectableKind::Entity; // the ref kind this mode selects
    MeshElementKind Next       = MeshElementKind::Vertex; // canonical cycle successor
    const char*     Label      = "";                      // layer-agnostic display string
};

[[nodiscard]] const MeshElementKindTraits& Traits(MeshElementKind kind);

// The kinds in canonical (cycle) order — drives mode toolbars/iteration so the
// order has exactly one home and can't drift from CycleElementKind.
[[nodiscard]] const std::array<MeshElementKind, MeshElementKindCount>& AllMeshElementKinds();

// Inverse of Traits().Selectable: which mode owns a given selectable kind
// (Entity -> Object). Lets viewport/UI adapters map a ref to a mode without a switch.
[[nodiscard]] MeshElementKind MeshElementKindForSelectable(SelectableKind selectable);
