#include "ChromeBars.h"

#include "ChromeGeometry.h"
#include "ChromeOrnaments.h"
#include "ChromePaint.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>
#include <cmath>

namespace
{
// The chassis dimensions the theme asks for, in screen pixels. Rounded: the
// lane sits between 1px bevels, and a fractional editor.ui.scale would
// otherwise put every one of them on a half pixel.
EditorChrome::BarSpec SpecFor(float itemHeight)
{
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float rim = std::round(EditorUi::Px(m.BarRim));
    const float clearance = std::round(EditorUi::Px(m.BarClearance));
    // The cap terminates the channel, so it is proportioned from the channel
    // rather than from a metric of its own.
    const float cap = std::round((itemHeight + clearance * 2.0f) * 0.6f);
    return EditorChrome::BarSpec{ .Rim = rim, .Clearance = clearance, .Cap = cap };
}
}

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

namespace
{
// The lit end of a gradient channel: the metal carried most of the way toward
// the palette's dim accent, so the surface reads as dark blue rather than as
// grey. Derived from two palette roles rather than a literal, so a theme
// retunes it by retuning its metal and its accent.
ImU32 GradientLit()
{
    return ImGui::GetColorU32(EditorUi::Darken(EditorUi::Mix(EditorUi::MetalBase, EditorUi::AccentDim, 0.85f), 0.15f));
}

ImU32 GradientDark()
{
    return ImGui::GetColorU32(EditorUi::Darken(EditorUi::Mix(EditorUi::MetalBase, EditorUi::AccentDim, 0.3f), 0.55f));
}

// The channel's face. Everything else about the bar is identical across
// finishes; this is the only thing a theme's surface choice changes.
void PaintChannel(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const BarSurface& surface)
{
    const ImU32 floorColor = ImGui::GetColorU32(EditorUi::Darken(EditorUi::MetalBase, 0.42f));
    const bool textured = surface.Finish == EditorUi::BarFinish::Texture && surface.Texture != 0;
    switch (surface.Finish)
    {
    case EditorUi::BarFinish::GradientX:
        HorizontalGradient(dl, mn, mx, GradientLit(), GradientDark());
        return;
    case EditorUi::BarFinish::GradientY:
        VerticalGradient(dl, mn, mx, GradientLit(), GradientDark());
        return;
    case EditorUi::BarFinish::Texture:
    case EditorUi::BarFinish::Solid:
        break;
    }
    // The floor goes under an authored strip too: the art may carry alpha, and
    // a theme whose texture failed to resolve lands here as plain Solid.
    dl->AddRectFilled(mn, mx, floorColor);
    if (!textured)
        return;
    const ImVec2 uv1 = SurfaceTileUv(ImVec2(mx.x - mn.x, mx.y - mn.y), surface.TextureSize);
    dl->AddImage(surface.Texture, mn, mx, ImVec2(0.0f, 0.0f), uv1, surface.Tint);
}
}

BarRects BarFrame(ImDrawList* dl, ImVec2 mn, ImVec2 mx, BarEdge lipEdge, float itemHeight,
                  const BarSurface& surface)
{
    const BarSpec spec = SpecFor(itemHeight);
    const BarRects bar = BarLayout(mn, mx, spec, itemHeight);
    if (mx.x <= mn.x || mx.y <= mn.y)
        return bar;

    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));

    // The band first, then the channel cut out of it. The channel's fill is
    // opaque and spans everything between the rims, so the band's gradient
    // survives only in the rims, where it reads as the shading on a piece of
    // metal rather than as the bar itself.
    BarBackdrop(dl, mn, mx, lipEdge);

    const ImVec2 channelMin(mn.x, bar.TopRimMax.y);
    const ImVec2 channelMax(mx.x, bar.BottomRimMin.y);
    if (channelMax.y <= channelMin.y)
        return bar;

    PaintChannel(dl, channelMin, channelMax, surface);
    InsetWell(dl, channelMin, channelMax, ImGui::GetColorU32(EditorUi::MetalShadow),
              ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.35f)), edge);

    // The seam where the top rim meets the channel: the illuminated joint
    // between two pieces, the same cyan the lip runs on.
    const float seamY = std::floor(channelMin.y + edge) + 0.5f;
    dl->AddLine(ImVec2(channelMin.x, seamY), ImVec2(channelMax.x, seamY),
                ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, 0.28f)), edge);

    if (bar.HasCaps)
    {
        const float capH = bar.LeftCapMax.y - bar.LeftCapMin.y;
        const float chamfer = std::min(EditorUi::Px(m.Chamfer) * 0.5f, capH * 0.3f);
        const float boltR = std::min(EditorUi::Px(m.ScrewRadius) * 1.35f, capH * 0.3f);
        const ImU32 body = ImGui::GetColorU32(EditorUi::Lighten(EditorUi::MetalBase, 0.06f));
        const ImU32 highlight = ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.7f));
        const ImU32 shadow = ImGui::GetColorU32(EditorUi::MetalShadow);
        const ImU32 tint = ImGui::GetColorU32(EditorUi::Accent);
        const float half = edge * 0.5f;
        const auto cap = [&](ImVec2 capMin, ImVec2 capMax) {
            FillChamfered(dl, ChamferOutline(capMin, capMax, chamfer), body);
            BevelChamfered(dl, ChamferOutline(ImVec2(capMin.x + half, capMin.y + half),
                                              ImVec2(capMax.x - half, capMax.y - half), chamfer),
                           highlight, shadow, edge);
            if (boltR >= 1.0f)
            {
                const ImVec2 c((capMin.x + capMax.x) * 0.5f, (capMin.y + capMax.y) * 0.5f);
                DrawOrnament(dl, OrnamentKind::Bolt, ImVec2(c.x - boltR, c.y - boltR),
                             ImVec2(c.x + boltR, c.y + boltR), tint);
            }
        };
        cap(bar.LeftCapMin, bar.LeftCapMax);
        cap(bar.RightCapMin, bar.RightCapMax);
    }
    return bar;
}

float BarLaneInset()
{
    const BarSpec spec = SpecFor(0.0f);
    return spec.Rim + spec.Clearance;
}

float BarHeight(float itemHeight)
{
    return BarHeightFor(SpecFor(itemHeight), itemHeight);
}

float BarButtonSize()
{
    return ImGui::GetFrameHeight();
}

void BarMarkings(ImDrawList* dl, const BarRects& bar, float fromX, float toX)
{
    const float slash = EditorUi::Px(EditorUi::Metrics.VentLength * 0.5f);
    const float gap = EditorUi::Px(6.0f);
    // A run only carries a marking when it is comfortably longer than one, so
    // the bars stay quiet as their contents grow.
    if (slash <= 0.0f || toX - fromX < slash + gap * 3.0f)
        return;
    const float h = bar.LaneMax.y - bar.LaneMin.y;
    const float inset = std::floor(h * 0.3f);
    DrawOrnament(dl, OrnamentKind::TripleSlash, ImVec2(toX - gap - slash, bar.LaneMin.y + inset),
                 ImVec2(toX - gap, bar.LaneMax.y - inset),
                 ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, 0.35f)));
}

float MenuBarStripWidth(std::span<const char* const> labels)
{
    if (labels.empty())
        return 0.0f;
    // A horizontal BeginMenu draws its label at its natural width and doubles
    // the item spacing around it, so a run of them is the labels plus two
    // spacings per gap. Pinned by ChromeBarsTests against the vendored ImGui.
    float width = 0.0f;
    for (const char* label : labels)
        width += ImGui::CalcTextSize(label, nullptr, true).x;
    return width + ImGui::GetStyle().ItemSpacing.x * 2.0f * static_cast<float>(labels.size() - 1);
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

void DrawBay(ImDrawList* dl, ImVec2 mn, ImVec2 mx, bool active)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));
    const float chamfer = std::min(EditorUi::Px(m.Chamfer) * 0.5f, (mx.y - mn.y) * 0.25f);

    // A bay cut into the channel: darker floor, shadow along the top and left
    // where the surrounding metal overhangs, a lit rim when active.
    FillChamfered(dl, ChamferOutline(mn, mx, chamfer), ImGui::GetColorU32(EditorUi::Darken(EditorUi::MetalBase, 0.45f)));
    const float half = edge * 0.5f;
    const ChamferPoly rim = ChamferOutline(ImVec2(mn.x + half, mn.y + half), ImVec2(mx.x - half, mx.y - half), chamfer);
    BevelChamfered(dl, rim, ImGui::GetColorU32(EditorUi::MetalShadow),
                   ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.7f)), edge);
    dl->AddLine(ImVec2(mn.x + chamfer, mn.y + edge * 2.0f), ImVec2(mx.x - chamfer, mn.y + edge * 2.0f),
                ImGui::GetColorU32(EditorUi::MetalShadow), edge * 2.0f);
    if (active)
        GlowChamfered(dl, rim, EditorUi::Accent, m.GlowAlpha, EditorUi::Px(m.GlowWidth) * 0.7f);
}

ModuleScope::ModuleScope(const char* id)
    : Dl(ImGui::GetWindowDrawList())
    , Origin(ImGui::GetCursorScreenPos())
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
    // Sampled inside the group: an empty group's rect is not reliably empty
    // (ImGui folds the preceding item's rect into it), but a module that
    // submitted nothing has not moved the cursor.
    const ImVec2 end = ImGui::GetCursorScreenPos();
    const bool drewSomething = end.x != Origin.x || end.y != Origin.y;
    ImGui::EndGroup();

    if (drewSomething)
    {
        Splitter.SetCurrentChannel(Dl, 0);
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float pad = EditorUi::Px(EditorUi::Metrics.ModulePad);
        DrawBay(Dl, ImVec2(itemMin.x - pad, itemMin.y - pad), ImVec2(itemMax.x + pad, itemMax.y + pad), Active);
    }
    else
    {
        // EndGroup still emits an item for an empty group. Put the cursor back
        // so an unconditionally opened module is invisible to the layout: the
        // host sees an unmoved cursor and knows not to space anything off it.
        ImGui::SetCursorScreenPos(Origin);
    }
    Splitter.Merge(Dl);
    ImGui::PopID();
}
} // namespace EditorChrome
