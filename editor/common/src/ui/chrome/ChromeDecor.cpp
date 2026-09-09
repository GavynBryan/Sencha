#include "ChromeDecor.h"

#include "ui/EditorUiStyle.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

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
    case DecorSlot::ConsoleEmpty: return d.ConsoleEmpty;
    case DecorSlot::StatusTagline: return d.StatusTagline;
    }
    return {};
}

void EmptyRegionLabel(DecorSlot slot)
{
    const std::string_view text = DecorText(slot);
    if (text.empty())
        return;

    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start <= text.size();)
    {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    const EditorUi::TextRole role = EditorUi::TextRole::ApplicationTitle;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float lineHeight = EditorUi::MeasureRoleText(role, "X").y;
    const float gap = EditorUi::Px(4.0f);
    const float totalHeight = lineHeight * static_cast<float>(lines.size()) + gap * static_cast<float>(lines.size() - 1);
    float maxWidth = 0.0f;
    for (std::string_view line : lines)
        maxWidth = std::max(maxWidth, EditorUi::MeasureRoleText(role, line).x);
    if (avail.x < maxWidth + EditorUi::Px(16.0f) || avail.y < totalHeight + EditorUi::Px(16.0f))
        return;

    // Low contrast: the copy sits behind the eye, never competing with a control.
    const ImU32 color = ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::TextDim, 0.35f));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = std::floor(origin.y + (avail.y - totalHeight) * 0.5f);
    for (std::string_view line : lines)
    {
        const float w = EditorUi::MeasureRoleText(role, line).x;
        EditorUi::DrawRoleText(dl, ImVec2(std::floor(origin.x + (avail.x - w) * 0.5f), y), role, line, color);
        y += lineHeight + gap;
    }
}
} // namespace EditorChrome
