#pragma once

#include "TransformMode.h"
#include "TransformSpace.h"
#include "tools/CommandChoice.h"

#include <array>

// The transform gizmo modes and spaces as a user sees them: the tables the
// actions and the gizmo wheel both read, so what modes and spaces exist, in
// what order, with what icon, is defined once.
struct TransformModeItem
{
    TransformMode Mode;
    CommandChoice Choice;
};

inline constexpr std::array<TransformModeItem, 4> kTransformModeItems = {{
    { TransformMode::Resize, { "Resize", IconId::Resize } },
    { TransformMode::Move, { "Move", IconId::Move } },
    { TransformMode::Rotate, { "Rotate", IconId::Rotate } },
    { TransformMode::Scale, { "Scale", IconId::Scale } },
}};

struct TransformSpaceItem
{
    TransformSpace Space;
    const char* Action;
    CommandChoice Choice;
};

inline constexpr std::array<TransformSpaceItem, 3> kTransformSpaceItems = {{
    { TransformSpace::Grid, "gizmo.space.grid", { "Grid", IconId::Grid } },
    { TransformSpace::World, "gizmo.space.world", { "World", IconId::World } },
    { TransformSpace::Local, "gizmo.space.local", { "Local", IconId::Local } },
}};
