#include "ElementPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeHeader.h"

#include <imgui.h>

#include <array>
#include <cstring>
#include <optional>
#include <string>

namespace
{
    constexpr std::array<const char*, 9> kComputedProperties{
        "display", "position", "width", "height", "font-family", "font-size", "pointer-events", "tab-index",
        "overflow",
    };

    bool HasAttribute(const UiElementInfo& info, std::string_view name)
    {
        for (const auto& [key, value] : info.Attributes)
            if (key == name)
                return true;
        return false;
    }

    bool HasEventAttribute(const UiElementInfo& info)
    {
        for (const auto& [key, value] : info.Attributes)
            if (key.rfind("data-event-", 0) == 0 || key.rfind("on", 0) == 0)
                return true;
        return false;
    }
}

ElementPanel::ElementPanel(UiPreviewSession& session, PreviewViewState& view)
    : Session(session)
    , View(view)
{
}

void ElementPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
    if (!panel.IsOpen())
        return;
    if (!View.Selected.IsValid())
    {
        ImGui::TextDisabled("Nothing selected. Inspect the Preview or pick from the Outline.");
        return;
    }
    const std::optional<UiElementInfo> info = Session.Service().DescribeElement(View.Selected);
    if (!info)
    {
        // The element went away under the selection: a rebuild, a removed
        // row, a closed screen. Say so rather than showing the last frame.
        ImGui::TextDisabled("The selected element is gone (rebuilt or removed).");
        return;
    }

    std::string heading = info->Tag;
    if (!info->Id.empty())
        heading += "#" + info->Id;
    EditorUi::RoleLabel(EditorUi::TextRole::SectionTitle, heading);
    if (!info->Classes.empty())
    {
        std::string classes;
        for (const std::string& cls : info->Classes)
            classes += "." + cls + " ";
        EditorUi::RoleLabel(EditorUi::TextRole::SecondaryText, classes);
    }
    ImGui::Text("depth %u", info->Depth);

    DrawBoxes(*info);
    DrawComputed(*info);
    DrawNotes(*info);

    if (!info->Attributes.empty())
    {
        EditorChrome::SectionTitle("Attributes");
        if (ImGui::BeginTable("attrs", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
        {
            for (const auto& [key, value] : info->Attributes)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(key.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value.c_str());
            }
            ImGui::EndTable();
        }
    }
}

void ElementPanel::DrawBoxes(const UiElementInfo& info)
{
    EditorChrome::SectionTitle("Boxes");
    const float scale = Session.DisplayScale();
    struct Row
    {
        const char* Name;
        const UiElementBox& Box;
    };
    const std::array<Row, 4> rows{ {
        { "margin", info.Boxes.Margin },
        { "border", info.Boxes.Border },
        { "padding", info.Boxes.Padding },
        { "content", info.Boxes.Content },
    } };
    if (ImGui::BeginTable("boxes", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("box");
        ImGui::TableSetupColumn("px");
        ImGui::TableSetupColumn("dp");
        ImGui::TableHeadersRow();
        for (const Row& row : rows)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Name);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f, %.0f  %.0f x %.0f", row.Box.X, row.Box.Y, row.Box.Width, row.Box.Height);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f, %.1f  %.1f x %.1f", row.Box.X / scale, row.Box.Y / scale, row.Box.Width / scale,
                        row.Box.Height / scale);
        }
        ImGui::EndTable();
    }
}

void ElementPanel::DrawComputed(const UiElementInfo& info)
{
    EditorChrome::SectionTitle("Computed");
    UiService& ui = Session.Service();
    if (ImGui::BeginTable("computed", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        for (const char* property : kComputedProperties)
        {
            const std::optional<std::string> value = ui.ComputedProperty(info.Ref, property);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(property);
            ImGui::TableNextColumn();
            if (value)
                ImGui::TextUnformatted(value->c_str());
            else
                ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }
}

void ElementPanel::DrawNotes(const UiElementInfo& info)
{
    UiService& ui = Session.Service();
    const std::optional<std::string> tabIndex = ui.ComputedProperty(info.Ref, "tab-index");
    const bool focusable = tabIndex && *tabIndex != "none";
    const bool activatable = HasEventAttribute(info) || info.Tag == "button" || info.Tag == "input"
                             || info.Tag == "select" || HasAttribute(info, "data-value");

    const std::optional<std::string> pointerEvents = ui.ComputedProperty(info.Ref, "pointer-events");
    const bool root = !info.Parent.IsValid() || info.Depth <= 1;
    const bool swallows = root && pointerEvents && *pointerEvents != "none";

    if (!activatable || focusable)
    {
        if (!swallows)
            return;
    }
    EditorChrome::SectionTitle("Notes");
    if (activatable && !focusable)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::Warning);
        ImGui::TextWrapped("Activatable but not focusable: no tab-index, so a controller cannot reach it.");
        ImGui::PopStyleColor();
    }
    if (swallows)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::Warning);
        ImGui::TextWrapped("A root with pointer-events: %s takes every pointer event on the surface. A menu wants "
                           "that; an overlay over a game does not.",
                           pointerEvents->c_str());
        ImGui::PopStyleColor();
    }
}
