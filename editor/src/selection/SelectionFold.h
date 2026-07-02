#pragma once

#include "ISelectionContext.h"
#include "SelectableRef.h"

#include <cstdint>
#include <vector>

namespace SelectionFold
{
// How a freshly-gathered set folds into the current selection.
enum class Op : uint8_t
{
    Replace, // gathered becomes the selection
    Add,     // gathered joins the selection
    Toggle,  // per item: absent adds, present removes
    Remove,  // gathered leaves the selection
};

// Folds `gathered` into `current` per `op`. Dedups and repairs the primary, so
// callers can pass a loose gathered set. Shared by the select tool's click, the
// marquee interaction, loop selection, and select-all, so every gesture resolves
// selection identically.
[[nodiscard]] SelectionSnapshot Apply(const SelectionSnapshot& current,
                                      const std::vector<SelectableRef>& gathered,
                                      Op op);

// Modifier decode for a single-item click: Ctrl toggles (the reversible gesture
// for one item), Shift adds, Ctrl+Shift removes explicitly. Plain bools so this
// header stays free of input-layer types.
[[nodiscard]] Op OpForClick(bool ctrl, bool shift);

// Modifier decode for bulk gathers (marquee, loop selection, whole-mesh
// expansion): Ctrl removes rather than toggles, because toggling dozens of
// items at once produces a selection nobody intended.
[[nodiscard]] Op OpForBulk(bool ctrl, bool shift);
}
