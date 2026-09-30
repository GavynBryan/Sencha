#pragma once

#include "TransformMode.h"
#include "tools/CommandChoice.h"

#include <array>

// The transform gizmo modes as a user sees them: the one table the toolbar
// strip and the gizmo wheel both read, so what modes exist, in what order,
// with what icon is defined once. The tooltip carries the strip's key hint.
struct TransformModeItem
{
    TransformMode Mode;
    CommandChoice Choice;
    const char* Tooltip;
};

inline constexpr std::array<TransformModeItem, 4> kTransformModeItems = {{
    { TransformMode::Resize, { "Resize", IconId::Resize }, "Resize bounds  [Shift+Q]" },
    { TransformMode::Move, { "Move", IconId::Move }, "Move  [Shift+W]" },
    { TransformMode::Rotate, { "Rotate", IconId::Rotate }, "Rotate  [Shift+E]" },
    { TransformMode::Scale, { "Scale", IconId::Scale }, "Scale  [Shift+R]" },
}};
