#include "ActionLogPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeControls.h"

#include <imgui.h>

#include <cstdio>
#include <string>

namespace
{
    std::string FormatArgument(const UiValue& value)
    {
        char buffer[96];
        switch (value.Kind())
        {
        case UiValueKind::None: return "none";
        case UiValueKind::Bool: return value.AsBool() ? "bool true" : "bool false";
        case UiValueKind::Int:
            std::snprintf(buffer, sizeof(buffer), "int %lld", static_cast<long long>(value.AsInt()));
            return buffer;
        case UiValueKind::Float:
            std::snprintf(buffer, sizeof(buffer), "float %.6g", value.AsFloat());
            return buffer;
        case UiValueKind::String: return "string \"" + std::string(value.AsString()) + "\"";
        case UiValueKind::Id:
            std::snprintf(buffer, sizeof(buffer), "id %llu", static_cast<unsigned long long>(value.AsId()));
            return buffer;
        }
        return "?";
    }
}

ActionLogPanel::ActionLogPanel(UiPreviewSession& session)
    : Session(session)
{
}

void ActionLogPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
    if (!panel.IsOpen())
        return;

    if (EditorChrome::Button("clear", "Clear", { EditorUi::Px(56.0f), 0.0f }, EditorChrome::ButtonTone::Normal))
        Session.ClearActions();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu raised", Session.Actions().size());

    const UiPreviewModel& model = Session.Model();
    if (!ImGui::BeginTable("actions", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg
                                            | ImGuiTableFlags_ScrollY))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(40.0f));
    ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("arguments", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableHeadersRow();
    std::size_t index = 0;
    for (const UiAction& action : Session.Actions())
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%zu", ++index);
        ImGui::TableNextColumn();
        // Id is the declared index plus one; the model is the declaration.
        const std::size_t declared = action.Id.Value;
        if (declared >= 1 && declared <= model.Actions.size())
            ImGui::TextUnformatted(model.Actions[declared - 1].c_str());
        else
            ImGui::Text("action %zu", declared);
        ImGui::TableNextColumn();
        std::string arguments;
        for (std::size_t i = 0; i < action.Arguments.size(); ++i)
        {
            if (i > 0)
                arguments += ", ";
            arguments += FormatArgument(action.Arguments[i]);
        }
        ImGui::TextUnformatted(arguments.empty() ? "-" : arguments.c_str());
    }
    // Follow the newest while the user has not scrolled away.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndTable();
}
