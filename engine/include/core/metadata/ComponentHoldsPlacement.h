#pragma once

#include <concepts>

// Specialized with `static bool Holds(const Component&)`: whether an entity built from that value has a transform
// nothing can recompute, so a save keeps it (docs/gameplay/saves.md, "Placement").
template <typename Component>
struct ComponentHoldsPlacement;

template <typename Component>
concept DeclaresHoldsPlacement = requires(const Component& value) {
    { ComponentHoldsPlacement<Component>::Holds(value) } -> std::same_as<bool>;
};
