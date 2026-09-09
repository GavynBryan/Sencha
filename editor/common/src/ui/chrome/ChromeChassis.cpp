#include "ChromeChassis.h"

#include "ChromeGeometry.h"
#include "ChromeOrnaments.h"
#include "ChromePaint.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>

namespace EditorChrome
{
ChassisSpec ChassisSpecNow()
{
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    return ChassisSpec{
        .Border = EditorUi::Px(m.ChassisBorder),
        .Chamfer = EditorUi::Px(m.ChassisChamfer),
        .Recess = EditorUi::Px(m.Recess),
    };
}

float ChassisInset()
{
    const ChassisSpec spec = ChassisSpecNow();
    return spec.Border + spec.Recess;
}

void DrawChassisBase(ImDrawList* dl, ImVec2 mn, ImVec2 mx)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    const ChassisSpec spec = ChassisSpecNow();
    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(EditorUi::ChassisBg));
    FrameRing(dl, mn, mx, spec.Border + spec.Recess, spec.Chamfer, ImGui::GetColorU32(EditorUi::MetalBase),
              ImGui::GetColorU32(EditorUi::ChassisBg));
    // The well the panels sit in shows the ground through the seams between
    // dock nodes.
    const float inset = spec.Border + spec.Recess;
    dl->AddRectFilled(ImVec2(mn.x + inset, mn.y + inset), ImVec2(mx.x - inset, mx.y - inset),
                      ImGui::GetColorU32(EditorUi::ChassisBg));
}

void DrawChassisEdges(ImDrawList* dl, ImVec2 mn, ImVec2 mx)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    const ChassisSpec spec = ChassisSpecNow();
    const float edge = std::max(1.0f, EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    const float half = edge * 0.5f;
    BevelChamfered(dl, ChamferOutline(ImVec2(mn.x + half, mn.y + half), ImVec2(mx.x - half, mx.y - half), spec.Chamfer),
                   ImGui::GetColorU32(EditorUi::MetalHighlight), ImGui::GetColorU32(EditorUi::MetalShadow), edge);
    const float inset = spec.Border + spec.Recess;
    InsetWell(dl, ImVec2(mn.x + inset, mn.y + inset), ImVec2(mx.x - inset, mx.y - inset),
              ImGui::GetColorU32(EditorUi::MetalShadow),
              ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.45f)), edge);
}

void DrawChassisOrnaments(ImDrawList* dl, ImVec2 mn, ImVec2 mx)
{
    const ChassisSpec spec = ChassisSpecNow();
    const float ring = spec.Border + spec.Recess;
    const float r = std::min(EditorUi::Px(EditorUi::Metrics.ScrewRadius), ring * 0.5f);
    if (r < 1.0f || mx.x - mn.x < spec.Chamfer * 4.0f || mx.y - mn.y < spec.Chamfer * 4.0f)
        return;
    // One screw past each chamfer, centered on the ring.
    const float along = spec.Chamfer + r * 2.0f;
    const float mid = ring * 0.5f;
    const ImU32 tint = ImGui::GetColorU32(EditorUi::Accent);
    const auto screw = [&](float cx, float cy) {
        DrawOrnament(dl, OrnamentKind::Screw, ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), tint);
    };
    screw(mn.x + along, mn.y + mid);
    screw(mx.x - along, mn.y + mid);
    screw(mn.x + along, mx.y - mid);
    screw(mx.x - along, mx.y - mid);
}
} // namespace EditorChrome
