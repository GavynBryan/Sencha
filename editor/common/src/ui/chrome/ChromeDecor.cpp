#include "ChromeDecor.h"

#include "ChromeOrnaments.h"
#include "ui/EditorUiStyle.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace
{
// Walks the slot's lines without copying: `visit(line, index)` per line.
template <typename Visit>
void ForEachLine(std::string_view text, Visit&& visit)
{
    std::size_t index = 0;
    for (std::size_t start = 0; start <= text.size(); ++index)
    {
        const std::size_t end = text.find('\n', start);
        visit(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start), index);
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
}

// The first line is the word, the rest the readout under it.
EditorUi::TextRole RoleForLine(std::size_t index)
{
    return index == 0 ? EditorUi::TextRole::ApplicationTitle : EditorUi::TextRole::Status;
}
}

namespace EditorChrome
{
std::string_view DecorText(DecorSlot slot)
{
    const EditorUi::DecorStrings& d = EditorUi::Decor;
    switch (slot)
    {
    case DecorSlot::HierarchyEmpty: return d.HierarchyEmpty;
    case DecorSlot::MaterialBrowserEmpty: return d.MaterialBrowserEmpty;
    case DecorSlot::SceneBrowserEmpty: return d.SceneBrowserEmpty;
    case DecorSlot::ToolPropertiesIdle: return d.ToolPropertiesIdle;
    }
    return {};
}

void EmptyRegionLabel(DecorSlot slot)
{
    const std::string_view text = DecorText(slot);
    if (text.empty())
        return;

    const float gap = EditorUi::Px(4.0f);
    float totalHeight = 0.0f;
    float maxWidth = 0.0f;
    ForEachLine(text, [&](std::string_view line, std::size_t index) {
        const ImVec2 size = EditorUi::MeasureRoleText(RoleForLine(index), line);
        totalHeight += size.y + (index > 0 ? gap : 0.0f);
        maxWidth = std::max(maxWidth, size.x);
    });

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + avail.x, origin.y + avail.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // The well is never bare: a faint blueprint grid and a registration tick
    // in each corner say the module is live even with nothing in it.
    const float tick = EditorUi::Px(8.0f);
    if (avail.x > tick * 4.0f && avail.y > tick * 4.0f)
    {
        DrawOrnament(dl, OrnamentKind::Grid, origin, end, ImGui::GetColorU32(EditorUi::Border));
        const ImU32 mark = ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, 0.4f));
        const float inset = EditorUi::Px(2.0f);
        const auto corner = [&](float x, float y, float dx, float dy) {
            dl->AddLine(ImVec2(x, y), ImVec2(x + dx * tick, y), mark, 1.0f);
            dl->AddLine(ImVec2(x, y), ImVec2(x, y + dy * tick), mark, 1.0f);
        };
        corner(std::floor(origin.x + inset) + 0.5f, std::floor(origin.y + inset) + 0.5f, 1.0f, 1.0f);
        corner(std::floor(end.x - inset) - 0.5f, std::floor(origin.y + inset) + 0.5f, -1.0f, 1.0f);
        corner(std::floor(origin.x + inset) + 0.5f, std::floor(end.y - inset) - 0.5f, 1.0f, -1.0f);
        corner(std::floor(end.x - inset) - 0.5f, std::floor(end.y - inset) - 0.5f, -1.0f, -1.0f);
    }

    if (avail.x < maxWidth + EditorUi::Px(16.0f) || avail.y < totalHeight + EditorUi::Px(16.0f))
        return;

    // Low contrast: the copy sits behind the eye, never competing with a control.
    const ImU32 color = ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::TextDim, 0.35f));
    float y = std::floor(origin.y + (avail.y - totalHeight) * 0.5f);
    ForEachLine(text, [&](std::string_view line, std::size_t index) {
        const EditorUi::TextRole role = RoleForLine(index);
        const ImVec2 size = EditorUi::MeasureRoleText(role, line);
        EditorUi::DrawRoleText(dl, ImVec2(std::floor(origin.x + (avail.x - size.x) * 0.5f), y), role, line, color);
        y += size.y + gap;
    });
}
} // namespace EditorChrome
