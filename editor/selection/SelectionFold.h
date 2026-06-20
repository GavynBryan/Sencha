#pragma once

#include "ISelectionContext.h"
#include "SelectableRef.h"

#include <vector>

namespace SelectionFold
{
// Folds a freshly-gathered set into the current selection per the modifiers:
// Shift = add, Ctrl = remove, neither = replace. Dedups and repairs the primary,
// so callers can pass a loose `gathered` set. Shared by the select tool's click and
// the marquee interaction so both resolve selection identically.
[[nodiscard]] SelectionSnapshot Apply(const SelectionSnapshot& current,
                                      const std::vector<SelectableRef>& gathered,
                                      bool add, bool remove);
}
