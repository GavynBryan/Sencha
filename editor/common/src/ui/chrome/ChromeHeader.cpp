#include "ChromeHeader.h"

#include "ChromeFrame.h"
#include "ChromeGeometry.h"
#include "ChromeOrnaments.h"
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
void DrawCapAndLine(ImDrawList* dl, const HeaderRegions& regions, const HeaderState& state, CapShape shape,
                    bool fillCap = true)
{
    const float edge = std::max(1.0f, EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    const ImVec4& accent = HeaderAccent(state);
    if (regions.HasCap && fillCap)
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

// The plate's fixed parts, shared by the row that draws itself and the caller
// that has to size one first.
struct RowMetrics
{
    float Chamfer = 0.0f;
    float PadX = 0.0f;
    float PadY = 0.0f;
    float CapWidth = 0.0f;
};

RowMetrics MetricsFor(const HeaderRowSpec& spec, float height)
{
    const PanelChromeSpec chrome = ChromeSpecFor(spec.Style);
    RowMetrics r;
    r.Chamfer = std::min(chrome.Frame.Chamfer, height * 0.5f);
    r.PadX = r.Chamfer + EditorUi::Px(4.0f);
    r.PadY = EditorUi::Px(4.0f);
    // A bezel's cap is wider: it is a machined end piece, not a tick.
    const float band = std::max(0.0f, height - r.PadY * 2.0f);
    r.CapWidth = spec.CapWidth > 0.0f ? spec.CapWidth
                                      : band * (chrome.Header == HeaderPlate::Bezel ? 0.9f : 0.6f);
    return r;
}

// The lit top-centre piece a bezel carries: an amber strip flanked by technical
// markings, so the row reads as the top plate of an assembly.
void DrawBezelAccent(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const HeaderState& state)
{
    const float h = mx.y - mn.y;
    const float edge = std::max(1.0f, EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    const float strip = std::min((mx.x - mn.x) * 0.2f, h * 2.5f);
    const float slash = EditorUi::Px(EditorUi::Metrics.VentLength * 0.4f);
    const float gap = EditorUi::Px(6.0f);
    if (strip <= 0.0f || mx.x - mn.x < strip + (slash + gap) * 2.0f)
        return;
    const float cx = (mn.x + mx.x) * 0.5f;
    const float y0 = mn.y + edge * 2.0f;
    const ImU32 lit = ImGui::GetColorU32(state.Focused ? EditorUi::SelectedOutline
                                                       : EditorUi::Darken(EditorUi::SelectedOutline, 0.5f));
    DrawOrnament(dl, OrnamentKind::LightStrip, ImVec2(cx - strip * 0.5f, y0),
                 ImVec2(cx + strip * 0.5f, y0 + edge * 2.0f), lit);
    const ImU32 mark = ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, 0.45f));
    DrawOrnament(dl, OrnamentKind::TripleSlash, ImVec2(cx - strip * 0.5f - gap - slash, y0),
                 ImVec2(cx - strip * 0.5f - gap, y0 + h * 0.4f), mark);
    DrawOrnament(dl, OrnamentKind::TripleSlash, ImVec2(cx + strip * 0.5f + gap, y0),
                 ImVec2(cx + strip * 0.5f + gap + slash, y0 + h * 0.4f), mark);
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
    const float gap = EditorUi::Px(4.0f);
    const float inset = EditorUi::Px(2.0f);
    // The cap's length is the composition's, so a new one needs a table row
    // here rather than a branch.
    const HeaderRegions regions = LayoutHeader(ImVec2(mn.x + inset, mn.y), ImVec2(mx.x - inset, mx.y),
                                               h * ChromeSpecFor(style).RailCapScale, 0.0f, ornamentWidth, 0.0f, gap);
    DrawCapAndLine(dl, regions, state, CapShape::Block);
}

HeaderRegions DrawHeaderRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, std::string_view title, EditorUi::TextRole role,
                            HeaderState state, const HeaderRowSpec& spec)
{
    const float h = mx.y - mn.y;
    if (h <= 0.0f || mx.x <= mn.x)
        return HeaderRegions{};
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));
    const RowMetrics row = MetricsFor(spec, h);

    const ChamferPoly plate = ChamferOutline(mn, mx, row.Chamfer);
    FillChamfered(dl, plate, ImGui::GetColorU32(EditorUi::HeaderBg));
    const float half = edge * 0.5f;
    BevelChamfered(dl, ChamferOutline(ImVec2(mn.x + half, mn.y + half), ImVec2(mx.x - half, mx.y - half), row.Chamfer),
                   ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.6f)),
                   ImGui::GetColorU32(EditorUi::MetalShadow), edge);

    const float gap = EditorUi::Px(6.0f);
    const ImVec2 size = EditorUi::MeasureRoleText(role, title);
    const HeaderRegions regions = LayoutHeader(ImVec2(mn.x + row.PadX, mn.y + row.PadY),
                                               ImVec2(mx.x - row.PadX, mx.y - row.PadY),
                                               row.CapWidth, size.x, 0.0f, spec.ReservedControlWidth, gap);
    // A caller that named its own cap width owns that region: the shell's
    // nameplate puts its mark there instead of the accent block.
    HeaderRegions painted = regions;
    painted.HasLine = painted.HasLine && spec.Rule;
    DrawCapAndLine(dl, painted, state, CapShape::Slanted, /*fillCap*/ spec.CapWidth <= 0.0f);
    DrawTitle(dl, regions, title, role, state);
    if (ChromeSpecFor(spec.Style).Header == HeaderPlate::Bezel)
        DrawBezelAccent(dl, mn, mx, state);
    return regions;
}

float HeaderRowHeight(PanelStyle style)
{
    return ChromeSpecFor(style).HeaderHeight;
}

float HeaderRowWidth(const HeaderRowSpec& spec, float height, float titleWidth, float minLineWidth, float gap)
{
    const RowMetrics row = MetricsFor(spec, height);
    // Mirrors LayoutHeader's packing: cap, title, line, control, a gap between
    // each part that is present.
    float width = row.PadX * 2.0f;
    int parts = 0;
    const auto add = [&](float part) {
        if (part <= 0.0f)
            return;
        width += part;
        ++parts;
    };
    add(row.CapWidth);
    add(titleWidth);
    add(std::max(0.0f, minLineWidth));
    add(spec.ReservedControlWidth);
    if (parts > 1)
        width += gap * static_cast<float>(parts - 1);
    return width;
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
