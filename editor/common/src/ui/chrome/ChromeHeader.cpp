#include "ChromeHeader.h"

#include "ChromeFrame.h"
#include "ChromeGeometry.h"
#include "ChromePaint.h"

#include <algorithm>

namespace
{
using namespace EditorChrome;

ImU32 CapColor(const HeaderState& state)
{
    return ImGui::GetColorU32(state.Focused ? EditorUi::AccentHover : EditorUi::Accent);
}

// The cap is a small chamfered block; the line a single crisp stroke that
// brightens with focus.
void DrawCapAndLine(ImDrawList* dl, const HeaderRegions& regions, const HeaderState& state)
{
    const float edge = std::max(1.0f, EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    if (regions.HasCap)
    {
        const float h = regions.CapMax.y - regions.CapMin.y;
        FillChamfered(dl, ChamferOutline(regions.CapMin, regions.CapMax, h * 0.35f), CapColor(state));
    }
    if (regions.HasLine)
    {
        const float cy = std::floor((regions.LineMin.y + regions.LineMax.y) * 0.5f) + 0.5f;
        const ImVec4 line = EditorUi::WithAlpha(EditorUi::Accent, state.Focused ? 0.85f : 0.45f);
        dl->AddLine(ImVec2(regions.LineMin.x, cy), ImVec2(regions.LineMax.x, cy), ImGui::GetColorU32(line), edge);
        if (state.Focused)
            dl->AddLine(ImVec2(regions.LineMin.x, cy), ImVec2(regions.LineMax.x, cy),
                        ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, EditorUi::Metrics.GlowAlpha * 0.5f)),
                        EditorUi::Px(EditorUi::Metrics.GlowWidth));
    }
}

void DrawTitle(ImDrawList* dl, const HeaderRegions& regions, std::string_view title, EditorUi::TextRole role,
               const HeaderState& state)
{
    if (!regions.HasTitle)
        return;
    const ImVec2 size = EditorUi::MeasureRoleText(role, title);
    const ImVec2 pos(regions.TitleMin.x, std::floor((regions.TitleMin.y + regions.TitleMax.y - size.y) * 0.5f));
    EditorUi::DrawRoleText(dl, pos, role, title, state.Focused ? ImGui::GetColorU32(EditorUi::AccentHover) : 0);
}
}

namespace EditorChrome
{
void DrawHeaderRail(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, HeaderState state, float ornamentWidth)
{
    (void)style;
    const float h = mx.y - mn.y;
    if (h <= 0.0f || mx.x <= mn.x)
        return;
    // The rail is a groove in the well: the chassis shows through it.
    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(EditorUi::ChassisBg));
    const float gap = EditorUi::Px(4.0f);
    const float inset = EditorUi::Px(2.0f);
    const HeaderRegions regions = LayoutHeader(ImVec2(mn.x + inset, mn.y), ImVec2(mx.x - inset, mx.y),
                                               h * 3.0f, 0.0f, ornamentWidth, 0.0f, gap);
    DrawCapAndLine(dl, regions, state);
}

HeaderRegions DrawHeaderRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, std::string_view title, EditorUi::TextRole role,
                            HeaderState state, float reservedControlWidth)
{
    const float h = mx.y - mn.y;
    if (h <= 0.0f || mx.x <= mn.x)
        return HeaderRegions{};
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));
    const float chamfer = std::min(EditorUi::Px(m.Chamfer), h * 0.5f);

    const ChamferPoly plate = ChamferOutline(mn, mx, chamfer);
    FillChamfered(dl, plate, ImGui::GetColorU32(EditorUi::HeaderBg));
    const float half = edge * 0.5f;
    BevelChamfered(dl, ChamferOutline(ImVec2(mn.x + half, mn.y + half), ImVec2(mx.x - half, mx.y - half), chamfer),
                   ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.6f)),
                   ImGui::GetColorU32(EditorUi::MetalShadow), edge);

    const float gap = EditorUi::Px(6.0f);
    const float padX = chamfer + EditorUi::Px(4.0f);
    const float padY = EditorUi::Px(4.0f);
    const ImVec2 size = EditorUi::MeasureRoleText(role, title);
    const HeaderRegions regions = LayoutHeader(ImVec2(mn.x + padX, mn.y + padY), ImVec2(mx.x - padX, mx.y - padY),
                                               (h - padY * 2.0f) * 0.6f, size.x, 0.0f, reservedControlWidth, gap);
    DrawCapAndLine(dl, regions, state);
    DrawTitle(dl, regions, title, role, state);
    return regions;
}

void DrawHeaderRule(ImDrawList* dl, ImVec2 mn, ImVec2 mx, std::string_view title, EditorUi::TextRole role,
                    HeaderState state)
{
    const float h = mx.y - mn.y;
    if (h <= 0.0f || mx.x <= mn.x)
        return;
    const float gap = EditorUi::Px(6.0f);
    const ImVec2 size = EditorUi::MeasureRoleText(role, title);
    const HeaderRegions regions = LayoutHeader(mn, mx, h * 0.5f, size.x, 0.0f, 0.0f, gap);
    DrawCapAndLine(dl, regions, state);
    DrawTitle(dl, regions, title, role, state);
}

void SectionTitle(const char* label)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    const float height = ImGui::GetTextLineHeight() + EditorUi::Px(4.0f);
    DrawHeaderRule(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + width, pos.y + height), label,
                   EditorUi::TextRole::SectionTitle, HeaderState{});
    ImGui::Dummy(ImVec2(width, height));
}
} // namespace EditorChrome
