#include "ChromeTile.h"

#include "ChromeGeometry.h"
#include "ChromePaint.h"
#include "ChromeSelection.h"
#include "IconDraw.h"
#include "ui/EditorUiStyle.h"

namespace EditorChrome
{
TileResult Tile(const TileSpec& spec)
{
    const TileRects rects = TileLayout(ImGui::GetCursorScreenPos(), spec.Size,
        spec.Label.empty() ? 0.0f : ImGui::GetTextLineHeight(), ImGui::GetFontSize(), EditorUi::Px(4.0f));
    ImGui::BeginDisabled(spec.Disabled);
    bool clicked = false;
    if (spec.Interactive)
        clicked = ImGui::InvisibleButton("##tile", rects.Size);
    else
        ImGui::Dummy(rects.Size);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (spec.Image)
        dl->AddImage(spec.Image, rects.FaceMin, rects.FaceMax, ImVec2(0, 0), ImVec2(1, 1),
                     ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
    else
    {
        dl->AddRectFilled(rects.FaceMin, rects.FaceMax, ImGui::GetColorU32(EditorUi::FrameBg));
        InsetWell(dl, rects.FaceMin, rects.FaceMax, ImGui::GetColorU32(EditorUi::MetalShadow),
                   ImGui::GetColorU32(EditorUi::MetalHighlight), EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    }
    if (spec.Selected)
        SelectionOutline(dl, rects.FaceMin, rects.FaceMax);
    else
        dl->AddRect(rects.FaceMin, rects.FaceMax,
            ImGui::GetColorU32(hovered && spec.Interactive ? EditorUi::ControlHover : EditorUi::Border),
            0.0f, 0, EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    if (spec.Badge != IconId::None)
    {
        const float offset = EditorUi::Px(1.0f);
        DrawIcon(dl, spec.Badge, ImVec2(rects.BadgeMin.x + offset, rects.BadgeMin.y + offset),
                 ImVec2(rects.BadgeMax.x + offset, rects.BadgeMax.y + offset), ImGui::GetColorU32(EditorUi::MetalShadow));
        DrawIcon(dl, spec.Badge, rects.BadgeMin, rects.BadgeMax, ImGui::GetColorU32(EditorUi::Accent));
    }
    if (!spec.Label.empty())
    {
        dl->PushClipRect(rects.LabelMin, rects.LabelMax, true);
        dl->AddText(rects.LabelMin, ImGui::GetColorU32(spec.Selected || hovered ? EditorUi::TextPrimary : EditorUi::TextDim),
                    spec.Label.data(), spec.Label.data() + spec.Label.size());
        dl->PopClipRect();
    }
    ImGui::EndDisabled();
    return { clicked, hovered };
}
} // namespace EditorChrome
