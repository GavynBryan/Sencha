#include "ChromeFrame.h"

#include "ChromeGeometry.h"
#include "ChromePaint.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>
#include <iterator>

namespace
{
// One row per composition. Every visual trait a PanelStyle implies is a column
// here, so a new composition is a row and a new trait is a column -- never a
// branch in a painter. The frame numbers scale the Standard metrics, so the
// family stays one design at several weights.
struct Weight
{
    float Chamfer;
    float Recess;
    float Rail;
    float Padding; // of the content padding inside the ring
    float Header;  // of Metrics.HeaderHeight, for a titled header row
    float RailCap; // cap width on a docked panel's rail, in rail heights
    EditorChrome::OrnamentMount Mount;
    EditorChrome::HeaderPlate Plate;
    bool Brackets;
};

using EditorChrome::HeaderPlate;
using EditorChrome::OrnamentMount;

constexpr Weight kWeights[] = {
    // Chamfer Recess Rail  Pad    Header RailCap Mount              Plate              Brackets
    {  1.0f,   1.0f,  1.0f, 1.0f,  1.0f,  3.0f,   OrnamentMount::Well, HeaderPlate::Plain, false }, // Standard
    {  0.75f,  1.0f,  1.0f, 1.0f,  1.0f,  2.0f,   OrnamentMount::Well, HeaderPlate::Plain, false }, // Tool
    {  0.5f,   0.5f,  0.0f, 0.35f, 1.0f,  2.0f,   OrnamentMount::Well, HeaderPlate::Plain, false }, // Viewport
    // The scene the editor is built around: a ring on the chassis's scale, with
    // its hardware mounted on that ring because the scene image covers the well.
    {  1.75f,  2.5f,  0.0f, 0.35f, 1.25f, 2.0f,   OrnamentMount::Ring, HeaderPlate::Bezel, true },  // ViewportPrimary
    {  0.5f,   0.75f, 0.75f, 0.75f, 1.0f, 2.0f,   OrnamentMount::Well, HeaderPlate::Plain, false }, // Compact
};
static_assert(std::size(kWeights) == static_cast<std::size_t>(PanelStyle::Count),
              "every PanelStyle needs a chrome spec row, in enum order");
}

namespace EditorChrome
{
PanelChromeSpec ChromeSpecFor(PanelStyle style)
{
    const Weight& w = kWeights[static_cast<int>(style)];
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const FrameSpec frame{
        .Chamfer = EditorUi::Px(m.Chamfer * w.Chamfer),
        .Border = EditorUi::Px(m.Border),
        .Recess = EditorUi::Px(m.Recess * w.Recess),
        .Rail = EditorUi::Px(m.RailHeight * w.Rail),
    };
    const float ring = frame.Border + frame.Recess;
    return PanelChromeSpec{
        .Frame = frame,
        .ContentPadding = ImVec2(ring + EditorUi::Px(8.0f * w.Padding), ring + EditorUi::Px(4.0f * w.Padding)),
        .Mount = w.Mount,
        .Header = w.Plate,
        .HeaderHeight = EditorUi::Px(m.HeaderHeight * w.Header),
        .RailCapScale = w.RailCap,
        .CornerBrackets = w.Brackets,
    };
}

void DrawFrameBase(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style)
{
    const FrameRects rects = FrameLayout(mn, mx, ChromeSpecFor(style).Frame);
    dl->AddRectFilled(rects.WellMin, rects.WellMax, ImGui::GetColorU32(EditorUi::PanelBg));
}

void DrawFrameEdges(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, bool focused)
{
    const PanelChromeSpec chrome = ChromeSpecFor(style);
    const FrameSpec& spec = chrome.Frame;
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

    // The corner cap: an accent band along the top-left cut, the mark that
    // says which corner is the module's head. It brightens with the frame.
    FillChamfered(dl, CornerWedge(mn, mx, spec.Chamfer, edge * 2.0f),
                  ImGui::GetColorU32(focused ? EditorUi::Accent : EditorUi::AccentDim));

    // Illuminated corner accents, for a composition heavy enough to carry them.
    if (chrome.CornerBrackets)
        DrawBracketCorners(dl, rects.WellMin, rects.WellMax, ring * 2.0f,
                           ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, focused ? 0.9f : 0.5f)),
                           std::max(1.0f, edge * 2.0f));

    if (focused)
        GlowChamfered(dl, outline, EditorUi::Accent, m.GlowAlpha, EditorUi::Px(m.GlowWidth));
}
} // namespace EditorChrome
