#pragma once

#include "CommandChoice.h"

#include <span>

// What a radial menu shows and what a choice on it does. The session that
// runs the hold-move-release gesture and the chrome that paints the wheel go
// through this and never learn what an entry is: a tool, a gizmo mode, or
// anything else that answers it.
//
// Topology is stable while a session is open: Count, Item and Variants
// answer the same from press to release, so the sectors and fans under the
// pointer never move. Active state (ActiveIndex, ActiveVariant) may change
// at any time; it is only paint.
struct IRadialMenuModel
{
    using MenuItem = CommandChoice;

    virtual ~IRadialMenuModel() = default;

    [[nodiscard]] virtual int Count() const = 0;
    [[nodiscard]] virtual MenuItem Item(int index) const = 0;
    // The entry in effect, or -1.
    [[nodiscard]] virtual int ActiveIndex() const = 0;
    [[nodiscard]] virtual std::span<const MenuItem> Variants(int /*index*/) const { return {}; }
    [[nodiscard]] virtual int ActiveVariant(int /*index*/) const { return -1; }
    // A release over `index`, with `variant` chosen or -1 for the entry alone.
    virtual void Select(int index, int variant) = 0;
};
