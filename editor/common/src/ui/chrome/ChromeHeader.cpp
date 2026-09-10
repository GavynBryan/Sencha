#include "ChromeHeader.h"

#include "ChromeFrame.h"
#include "ChromeGeometry.h"
#include "ChromePaint.h"

#include <algorithm>

namespace
{
using namespace EditorChrome;

// The header's voice: amber when it names the thing being edited, bright
// cyan when its panel is being worked in, cyan otherwise.
const ImVec4& HeaderAccent(const HeaderState& state)
{
    return state.Selected ? EditorUi::SelectedOutline : state.Focused ? EditorUi::AccentHover : EditorUi::Accent;
}

enum class CapShape
{
    Block,   // a chamfered block, the rail's cap
    Slanted, // a parallelogram, the titled row's cap
};

// The cap, then the rule with a short terminator tick at each end, so the
// line reads as a drawn segment rather than a fade. Both brighten with focus.
void DrawCapAndLine(ImDrawList* dl, const HeaderRegions& regions, const HeaderState& state, CapShape shape)
{
    const float edge = std::max(1.0f, EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    const ImVec4& accent = HeaderAccent(state);
    if (regions.HasCap)
    {
        const float h = regions.CapMax.y - regions.CapMin.y;
        const ChamferPoly cap = shape == CapShape::Slanted ? SlantedCap(regions.CapMin, regions.CapMax, h * 0.35f)
                                                          : ChamferOutline(regions.CapMin, regions.CapMax, h * 0.35f);
        FillChamfered(dl, cap, ImGui::GetColorU32(accent));
    }
    if (regions.HasLine)
    {
        const float cy = std::floor((regions.LineMin.y + regions.LineMax.y) * 0.5f) + 0.5f;
        const bool lit = state.Focused || state.Selected;
        const ImU32 line = ImGui::GetColorU32(EditorUi::WithAlpha(accent, lit ? 0.85f : 0.45f));
        dl->AddLine(ImVec2(regions.LineMin.x, cy), ImVec2(regions.LineMax.x, cy), line, edge);
        const float tick = std::min((regions.LineMax.y - regions.LineMin.y) * 0.3f, EditorUi::Px(3.0f));
        const float x0 = std::floor(regions.LineMin.x) + 0.5f;
        const float x1 = std::floor(regions.LineMax.x) - 0.5f;
        const ImU32 terminator = ImGui::GetColorU32(accent);
        dl->AddLine(ImVec2(x0, cy - tick), ImVec2(x0, cy + tick), terminator, edge);
        dl->AddLine(ImVec2(x1, cy - tick), ImVec2(x1, cy + tick), terminator, edge);
        if (lit)
            dl->AddLine(ImVec2(regions.LineMin.x, cy), ImVec2(regions.LineMax.x, cy),
                        ImGui::GetColorU32(EditorUi::WithAlpha(accent, EditorUi::Metrics.GlowAlpha * 0.5f)),
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
    EditorUi::DrawRoleText(dl, pos, role, title,
                           state.Selected || state.Focused ? ImGui::GetColorU32(HeaderAccent(state)) : 0);
}
}

namespace EditorChrome
{
void DrawHeaderRail(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, HeaderState state, float ornamentWidth)
{
    const float h = mx.y - mn.y;
    if (h <= 0.0f || mx.x <= mn.x)
        return;
    // The rail is a groove in the well: the chassis shows through it, its
    // lower lip catching a little light.
    VerticalGradient(dl, mn, mx, ImGui::GetColorU32(EditorUi::ChassisBg),
                     ImGui::GetColorU32(EditorUi::Lighten(EditorUi::ChassisBg, 0.06f)));
    // The lighter weights carry a shorter cap.
    const bool light = style == PanelStyle::Tool || style == PanelStyle::Compact || style == PanelStyle::Viewport;
    const float gap = EditorUi::Px(4.0f);
    const float inset = EditorUi::Px(2.0f);
    const HeaderRegions regions = LayoutHeader(ImVec2(mn.x + inset, mn.y), ImVec2(mx.x - inset, mx.y),
                                               h * (light ? 2.0f : 3.0f), 0.0f, ornamentWidth, 0.0f, gap);
    DrawCapAndLine(dl, regions, state, CapShape::Block);
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
    DrawCapAndLine(dl, regions, state, CapShape::Slanted);
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
    DrawCapAndLine(dl, regions, state, CapShape::Block);
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
void HeaderNotch()
{
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(mn, ImVec2(mn.x + EditorUi::Px(2.0f), mx.y),
                                              ImGui::GetColorU32(EditorUi::Accent));
}

} // namespace EditorChrome
