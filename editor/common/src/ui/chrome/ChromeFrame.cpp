#include "ChromeFrame.h"

#include "ChromePaint.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>

namespace
{
// Each weight scales the Standard metrics; the family stays one design with
// five sizes. Viewport has no rail so the scene keeps its rows.
struct Weight
{
    float Chamfer;
    float Recess;
    float Rail;
};
constexpr Weight kWeights[] = {
    { 1.0f, 1.0f, 1.0f },    // Standard
    { 1.5f, 1.5f, 1.25f },   // Major
    { 0.75f, 1.0f, 1.0f },   // Tool
    { 0.5f, 0.5f, 0.0f },    // Viewport
    { 0.5f, 0.75f, 0.75f },  // Compact
};
}

namespace EditorChrome
{
FrameSpec SpecFor(PanelStyle style)
{
    const Weight& w = kWeights[static_cast<int>(style)];
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    return FrameSpec{
        .Chamfer = EditorUi::Px(m.Chamfer * w.Chamfer),
        .Border = EditorUi::Px(m.Border),
        .Recess = EditorUi::Px(m.Recess * w.Recess),
        .Rail = EditorUi::Px(m.RailHeight * w.Rail),
    };
}

ImVec2 ContentPadding(PanelStyle style)
{
    const FrameSpec spec = SpecFor(style);
    const float ring = spec.Border + spec.Recess;
    return ImVec2(ring + EditorUi::Px(8.0f), ring + EditorUi::Px(4.0f));
}

void DrawFrameBase(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style)
{
    const FrameRects rects = FrameLayout(mn, mx, SpecFor(style));
    dl->AddRectFilled(rects.WellMin, rects.WellMax, ImGui::GetColorU32(EditorUi::PanelBg));
}

void DrawFrameEdges(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, bool focused)
{
    const FrameSpec spec = SpecFor(style);
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float ring = spec.Border + spec.Recess;
    const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));
    const FrameRects rects = FrameLayout(mn, mx, spec);

    FrameRing(dl, mn, mx, ring, spec.Chamfer, ImGui::GetColorU32(EditorUi::MetalBase),
              ImGui::GetColorU32(EditorUi::ChassisBg));

    // A faint sheen across the top of the plate, kept inside the chamfers.
    if (mx.x - mn.x > spec.Chamfer * 2.0f)
        VerticalGradient(dl, ImVec2(mn.x + spec.Chamfer, mn.y), ImVec2(mx.x - spec.Chamfer, mn.y + ring),
                         ImGui::GetColorU32(EditorUi::Lighten(EditorUi::MetalBase, 0.10f)),
                         ImGui::GetColorU32(EditorUi::MetalBase));

    const float half = edge * 0.5f;
    const ChamferPoly outline = ChamferOutline(ImVec2(mn.x + half, mn.y + half), ImVec2(mx.x - half, mx.y - half), spec.Chamfer);
    BevelChamfered(dl, outline, ImGui::GetColorU32(EditorUi::MetalHighlight),
                   ImGui::GetColorU32(EditorUi::MetalShadow), edge);

    InsetWell(dl, rects.WellMin, rects.WellMax, ImGui::GetColorU32(EditorUi::MetalShadow),
              ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.45f)), edge);

    if (focused)
        GlowChamfered(dl, outline, EditorUi::Accent, m.GlowAlpha, EditorUi::Px(m.GlowWidth));
}
} // namespace EditorChrome
