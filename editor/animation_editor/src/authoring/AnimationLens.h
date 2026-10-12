#pragma once

#include <cstdint>

// What the card, the drawer and the deck's lanes are about when nothing more
// particular is picked (docs/editor/animation-workspace.md, Lenses).
enum class AnimationLens : std::uint8_t
{
    Rules,
    Layers,
    Flow,
    Blends,
    RootMotion,
    Facts,
    Requests,
    Events,
};
