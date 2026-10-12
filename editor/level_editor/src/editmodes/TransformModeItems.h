#pragma once

#include "TransformMode.h"
#include "TransformSpace.h"
#include "tools/CommandChoice.h"

#include <SDL3/SDL_keycode.h>

#include <array>

// The transform gizmo modes and spaces as a user sees them: the tables the
// actions and the gizmo wheel both read, so what modes and spaces exist, in
// what order, with what icon, is defined once.
struct TransformModeItem
{
    TransformMode Mode;
    const char* Action;
    // Taken with Shift, which keeps it off the fly camera's bare keys.
    SDL_Keycode Key;
    CommandChoice Choice;
};

inline constexpr std::array<TransformModeItem, 4> kTransformModeItems = {{
    { TransformMode::Resize, "gizmo.resize", SDLK_Q, { "Resize", IconId::Resize } },
    { TransformMode::Move, "gizmo.move", SDLK_W, { "Move", IconId::Move } },
    { TransformMode::Rotate, "gizmo.rotate", SDLK_E, { "Rotate", IconId::Rotate } },
    { TransformMode::Scale, "gizmo.scale", SDLK_R, { "Scale", IconId::Scale } },
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
