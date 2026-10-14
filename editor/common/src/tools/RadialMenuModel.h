#pragma once

#include "CommandChoice.h"

#include <math/Vec.h>

#include <span>
#include <string>
#include <vector>

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
    // The wheel is opening at `pointer`: the last moment the entries may
    // change before the gesture ends. A model with nothing to offer there
    // leaves Count at zero and the wheel stays shut.
    virtual void Opening(Vec2d /*pointer*/) {}
    // What the hub reads out while the wheel is open, a line each.
    [[nodiscard]] virtual std::vector<std::string> Summary() const { return {}; }
};
