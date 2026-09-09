#pragma once

#include <imgui.h>

// The application chassis: the frame the whole workspace is mounted in, with
// more presence than any panel. Its own surface with its own metrics, built
// from the same geometry and painters as the panel frames so the family holds.
namespace EditorChrome
{
struct ChassisSpec
{
    float Border = 0.0f;  // the ring's width
    float Chamfer = 0.0f; // the ring's outer corner cut
    float Recess = 0.0f;  // the well's inset inside the ring
};

// The chassis dimensions now, in screen pixels.
[[nodiscard]] ChassisSpec ChassisSpecNow();

// How far the dock host sits inside the work area on every side.
[[nodiscard]] float ChassisInset();

// The ground and the ring, drawn behind everything the chassis holds.
void DrawChassisBase(ImDrawList* dl, ImVec2 mn, ImVec2 mx);

// The ring's bevel and the well's inset edge, drawn after the base.
void DrawChassisEdges(ImDrawList* dl, ImVec2 mn, ImVec2 mx);
} // namespace EditorChrome
