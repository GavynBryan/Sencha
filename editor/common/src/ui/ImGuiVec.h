#pragma once

#include <math/Vec.h>

#include <imgui.h>

// The ImGui shell's side of the boundary with the editor's own geometry, which
// is written in engine vectors so it outlives the shell.
[[nodiscard]] inline Vec2d ToVec2d(ImVec2 v) { return { v.x, v.y }; }
[[nodiscard]] inline ImVec2 ToImVec2(Vec2d v) { return { v.X, v.Y }; }
