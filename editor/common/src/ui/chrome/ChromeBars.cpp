#include "ChromeBars.h"

#include "ChromeGeometry.h"
#include "ChromePaint.h"
#include "ChromeOrnaments.h"
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

    VerticalGradient(dl, mn, mx, ImGui::GetColorU32(EditorUi::Lighten(EditorUi::MetalBase, 0.12f)),
                     ImGui::GetColorU32(EditorUi::Darken(EditorUi::MetalBase, 0.24f)));

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
    }
}

float BarButtonSize()
{
    return ImGui::GetFrameHeight();
}

void Divider()
{
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 size(EditorUi::Px(4.0f), ImGui::GetFrameHeight());
    ImGui::Dummy(size);
    DrawOrnament(ImGui::GetWindowDrawList(), OrnamentKind::Seam,
                  ImVec2(mn.x, mn.y + EditorUi::Px(3.0f)),
                  ImVec2(mn.x + size.x, mn.y + size.y - EditorUi::Px(3.0f)), 0);
}

void Readout(const char* label, const char* value) { Readout(label, value, LedState::Off); }

float ReadoutWidth(const char* label, const char* value, LedState state)
{
    const ImVec2 labelSize = EditorUi::MeasureRoleText(EditorUi::TextRole::Status, label);
    const ImVec2 valueSize = EditorUi::MeasureRoleText(EditorUi::TextRole::Data, value);
    return ReadoutLayout(ImVec2(0.0f, 0.0f), labelSize.x, valueSize.x, ImGui::GetFrameHeight(), EditorUi::Px(4.0f),
                         EditorUi::Px(5.0f), state == LedState::Off ? 0.0f : EditorUi::Px(EditorUi::Metrics.ScrewRadius * 2.0f))
        .Size.x;
}

void Readout(const char* label, const char* value, LedState state)
{
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 labelSize = EditorUi::MeasureRoleText(EditorUi::TextRole::Status, label);
    const ImVec2 valueSize = EditorUi::MeasureRoleText(EditorUi::TextRole::Data, value);
    const float height = ImGui::GetFrameHeight();
    const ReadoutRects rects = ReadoutLayout(mn, labelSize.x, valueSize.x, height, EditorUi::Px(4.0f),
        EditorUi::Px(5.0f), state == LedState::Off ? 0.0f : EditorUi::Px(EditorUi::Metrics.ScrewRadius * 2.0f));
    ImGui::Dummy(rects.Size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cellMin(mn.x, mn.y + EditorUi::Px(2.0f));
    const ImVec2 cellMax(mn.x + rects.Size.x, mn.y + height - EditorUi::Px(2.0f));
    dl->AddRectFilled(cellMin, cellMax, ImGui::GetColorU32(EditorUi::FrameBg));
    InsetWell(dl, cellMin, cellMax, ImGui::GetColorU32(EditorUi::MetalShadow),
               ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.35f)), EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    EditorUi::DrawRoleText(dl, ImVec2(rects.LabelMin.x, mn.y + (height - labelSize.y) * 0.5f),
                          EditorUi::TextRole::Status, label, ImGui::GetColorU32(EditorUi::TextDim));
    EditorUi::DrawRoleText(dl, ImVec2(rects.ValueMin.x, mn.y + (height - valueSize.y) * 0.5f),
                          EditorUi::TextRole::Data, value, ImGui::GetColorU32(EditorUi::TextPrimary));
    if (rects.HasLed)
        DrawOrnament(dl, OrnamentKind::StatusLed, rects.LedMin, rects.LedMax,
            ImGui::GetColorU32(state == LedState::Alert ? EditorUi::SelectedOutline : EditorUi::Accent));
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
        FillChamfered(Dl, ChamferOutline(mn, mx, chamfer), ImGui::GetColorU32(EditorUi::Darken(EditorUi::MetalBase, 0.45f)));
        const float half = edge * 0.5f;
        const ChamferPoly rim = ChamferOutline(ImVec2(mn.x + half, mn.y + half), ImVec2(mx.x - half, mx.y - half), chamfer);
        BevelChamfered(Dl, rim, ImGui::GetColorU32(EditorUi::MetalShadow),
                       ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.7f)), edge);
        Dl->AddLine(ImVec2(mn.x + chamfer, mn.y + edge * 2.0f), ImVec2(mx.x - chamfer, mn.y + edge * 2.0f),
                    ImGui::GetColorU32(EditorUi::MetalShadow), edge * 2.0f);
        if (Active)
            GlowChamfered(Dl, rim, EditorUi::Accent, m.GlowAlpha, EditorUi::Px(m.GlowWidth) * 0.7f);
    }
    Splitter.Merge(Dl);
    ImGui::PopID();
}
} // namespace EditorChrome
