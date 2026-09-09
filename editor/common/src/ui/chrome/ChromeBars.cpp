#include "ChromeBars.h"

#include "ChromeGeometry.h"
#include "ChromePaint.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>

namespace EditorChrome
{
void BarBackdrop(ImDrawList* dl, ImVec2 mn, ImVec2 mx, BarEdge lipEdge)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));

    VerticalGradient(dl, mn, mx, ImGui::GetColorU32(EditorUi::Lighten(EditorUi::MetalBase, 0.08f)),
                     ImGui::GetColorU32(EditorUi::Darken(EditorUi::MetalBase, 0.12f)));

    // The band's own bevel: lit along the top, shadowed along the bottom.
    const float half = edge * 0.5f;
    dl->AddLine(ImVec2(mn.x, mn.y + half), ImVec2(mx.x, mn.y + half),
                ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.9f)), edge);
    dl->AddLine(ImVec2(mn.x, mx.y - half), ImVec2(mx.x, mx.y - half), ImGui::GetColorU32(EditorUi::MetalShadow), edge);

    // The lip: a cyan strip on the edge that faces the workspace, so the bar
    // reads as the console's frame around the working area.
    const ImU32 lip = ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, 0.55f));
    const ImU32 lipGlow = ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, m.GlowAlpha * 0.4f));
    const float glow = EditorUi::Px(m.GlowWidth);
    switch (lipEdge)
    {
    case BarEdge::Bottom:
        dl->AddLine(ImVec2(mn.x, mx.y - half), ImVec2(mx.x, mx.y - half), lipGlow, glow);
        dl->AddLine(ImVec2(mn.x, mx.y - half), ImVec2(mx.x, mx.y - half), lip, edge);
        break;
    case BarEdge::Top:
        dl->AddLine(ImVec2(mn.x, mn.y + half), ImVec2(mx.x, mn.y + half), lipGlow, glow);
        dl->AddLine(ImVec2(mn.x, mn.y + half), ImVec2(mx.x, mn.y + half), lip, edge);
        break;
    case BarEdge::Right:
        dl->AddLine(ImVec2(mx.x - half, mn.y), ImVec2(mx.x - half, mx.y), lipGlow, glow);
        dl->AddLine(ImVec2(mx.x - half, mn.y), ImVec2(mx.x - half, mx.y), lip, edge);
        break;
    }
}

float BarButtonSize()
{
    return ImGui::GetFrameHeight();
}

ModuleScope::ModuleScope(const char* id)
    : Dl(ImGui::GetWindowDrawList())
{
    ImGui::PushID(id);
    ImGui::BeginGroup();
    // Controls draw on channel 1; the bay is painted on channel 0 at the end,
    // once the group's rect is known, and lands under them on merge.
    Splitter.Split(Dl, 2);
    Splitter.SetCurrentChannel(Dl, 1);
}

ModuleScope::~ModuleScope()
{
    ImGui::EndGroup();
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    Splitter.SetCurrentChannel(Dl, 0);
    if (itemMax.x > itemMin.x && itemMax.y > itemMin.y)
    {
        const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
        const float pad = EditorUi::Px(m.ModulePad);
        const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));
        const ImVec2 mn(itemMin.x - pad, itemMin.y - pad);
        const ImVec2 mx(itemMax.x + pad, itemMax.y + pad);
        const float chamfer = std::min(EditorUi::Px(m.Chamfer) * 0.5f, (mx.y - mn.y) * 0.25f);

        // A bay cut into the band: darker floor, shadow along the top and
        // left where the band's edge overhangs, a lit rim when active.
        FillChamfered(Dl, ChamferOutline(mn, mx, chamfer), ImGui::GetColorU32(EditorUi::Darken(EditorUi::MetalBase, 0.35f)));
        const float half = edge * 0.5f;
        const ChamferPoly rim = ChamferOutline(ImVec2(mn.x + half, mn.y + half), ImVec2(mx.x - half, mx.y - half), chamfer);
        BevelChamfered(Dl, rim, ImGui::GetColorU32(EditorUi::MetalShadow),
                       ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.7f)), edge);
        if (Active)
            GlowChamfered(Dl, rim, EditorUi::Accent, m.GlowAlpha, EditorUi::Px(m.GlowWidth) * 0.7f);
    }
    Splitter.Merge(Dl);
    ImGui::PopID();
}
} // namespace EditorChrome
