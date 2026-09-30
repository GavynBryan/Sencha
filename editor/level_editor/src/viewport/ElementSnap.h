#pragma once

#include <math/Vec.h>

#include <imgui.h>

#include <optional>

struct ToolContext;
struct EditorViewport;

// World point of the geometry element under the cursor matching the snap
// target in ctx.Grid: vertex position, edge midpoint, or face surface hit.
// nullopt when snapping is off, the target is Grid, or nothing is under the
// cursor, so callers fall back to their grid snap or a free move.
[[nodiscard]] std::optional<Vec3d> ElementSnapPoint(const ToolContext& ctx,
                                                    const EditorViewport& viewport,
                                                    ImVec2 point);
