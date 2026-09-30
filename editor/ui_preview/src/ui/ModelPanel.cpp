#include "ModelPanel.h"

#include "authoring/BindingMisses.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeControls.h"
#include "ui/chrome/ChromeHeader.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
    // A std::string field. The text is written back while it is being typed
    // and the result says when the edit was committed (defocus or Enter), so
    // a caller republishes once, not per keystroke.
    bool InputString(const char* label, std::string& value)
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        ImGui::InputText(label, buffer, sizeof(buffer));
        if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
            value = buffer;
        return ImGui::IsItemDeactivatedAfterEdit();
    }

    constexpr std::array<const char*, 5> kKindNames{ "none", "bool", "int", "float", "string" };

    int KindIndex(UiValueKind kind)
    {
        switch (kind)
        {
        case UiValueKind::None: return 0;
        case UiValueKind::Bool: return 1;
        case UiValueKind::Int: return 2;
        case UiValueKind::Float: return 3;
        case UiValueKind::String: return 4;
        case UiValueKind::Id: return 2;
        }
        return 0;
    }

    UiValue DefaultOfKind(int index)
    {
        switch (index)
        {
        case 1: return UiValue(false);
        case 2: return UiValue(std::int64_t{ 0 });
        case 3: return UiValue(0.0);
        case 4: return UiValue(std::string{});
        default: return UiValue{};
        }
    }

    // Edits one sample value in place; true when it changed and the edit is
    // done (a field commits on defocus, a checkbox on click).
    bool EditValue(UiValue& value)
    {
        switch (value.Kind())
        {
        case UiValueKind::Bool:
        {
            bool b = value.AsBool();
            if (ImGui::Checkbox("##v", &b))
            {
                value = UiValue(b);
                return true;
            }
            return false;
        }
        case UiValueKind::Int:
        case UiValueKind::Id:
        {
            std::int64_t i = value.AsInt();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputScalar("##v", ImGuiDataType_S64, &i);
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                value = UiValue(i);
                return true;
            }
            return false;
        }
        case UiValueKind::Float:
        {
            double d = value.AsFloat();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputDouble("##v", &d, 0.0, 0.0, "%.4g");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                value = UiValue(d);
                return true;
            }
            return false;
        }
        case UiValueKind::String:
        {
            std::string s(value.AsString());
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool committed = InputString("##v", s);
            if (s != value.AsString())
                value = UiValue(std::move(s));
            return committed;
        }
        case UiValueKind::None:
            ImGui::TextDisabled("none");
            return false;
        }
        return false;
    }

    // Comma-separated in one field: the list is short and read as one thing.
    std::string JoinChoices(const std::vector<std::string>& choices)
    {
        std::string out;
        for (std::size_t i = 0; i < choices.size(); ++i)
        {
            if (i > 0)
                out += ", ";
            out += choices[i];
        }
        return out;
    }

    std::vector<std::string> SplitChoices(const std::string& text)
    {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t comma = text.find(',', start);
            if (comma == std::string::npos)
                comma = text.size();
            std::string item = text.substr(start, comma - start);
            const std::size_t b = item.find_first_not_of(' ');
            const std::size_t e = item.find_last_not_of(' ');
            if (b != std::string::npos)
                out.push_back(item.substr(b, e - b + 1));
            start = comma + 1;
        }
        return out;
    }

    const char* MissKindLabel(BindingMissKind kind)
    {
        switch (kind)
        {
        case BindingMissKind::Variable: return "variable";
        case BindingMissKind::Member: return "member";
        case BindingMissKind::Action: return "action";
        }
        return "?";
    }
}

ModelPanel::ModelPanel(UiPreviewSession& session, PreviewViewState& view, Actions actions)
    : Session(session)
    , View(view)
    , Act(std::move(actions))
{
}

void ModelPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
    if (!panel.IsOpen())
        return;
    if (!Session.IsOpen())
    {
        ImGui::TextDisabled("No document open.");
        return;
    }
    if (!ImGui::BeginTabBar("##model_tabs"))
        return;
    if (ImGui::BeginTabItem("Model"))
    {
        DrawModelTab();
        ImGui::EndTabItem();
    }
    const ImGuiTabItemFlags bindingsFlags = View.ShowBindings ? ImGuiTabItemFlags_SetSelected : 0;
    if (ImGui::BeginTabItem("Bindings", nullptr, bindingsFlags))
    {
        View.ShowBindings = false;
        DrawBindingsTab();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void ModelPanel::DrawModelTab()
{
    UiPreviewModel& model = Session.Model();

    ImGui::SetNextItemWidth(EditorUi::Px(120.0f));
    if (InputString("data-model", model.ModelName))
        Redeclare();
    ImGui::SameLine();
    if (ImGui::Checkbox("modal", &model.Modal))
        Redeclare();

    // Their own row, right-aligned against the panel's content edge: the dock
    // this panel lives in is narrow, and sharing the row above puts the
    // buttons over the field's label.
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - EditorUi::Px(118.0f));
    if (EditorChrome::Button("save", "Save", { EditorUi::Px(56.0f), 0.0f }, EditorChrome::ButtonTone::Normal)
        && Act.Save)
    {
        std::string error;
        LastError = Act.Save(&error) ? std::string{} : error;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Write the sidecar beside the document (Ctrl+S)");
    ImGui::SameLine();
    if (EditorChrome::Button("reset", "Reset", { EditorUi::Px(56.0f), 0.0f }, EditorChrome::ButtonTone::Normal)
        && Act.Reset)
        Act.Reset();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reload the sidecar, discarding edits");
    if (!LastError.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::Danger);
        ImGui::TextWrapped("%s", LastError.c_str());
        ImGui::PopStyleColor();
    }

    DrawProperties();
    DrawArrays();
    DrawRows();
    DrawActions();
}

void ModelPanel::DrawProperties()
{
    UiPreviewModel& model = Session.Model();
    EditorChrome::SectionTitle("Properties");
    std::optional<std::size_t> remove;
    bool declarationChanged = false;
    if (ImGui::BeginTable("props", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(70.0f));
        ImGui::TableSetupColumn("sample", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("rw", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(28.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(24.0f));
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < model.Properties.size(); ++i)
        {
            UiModelProperty& property = model.Properties[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            declarationChanged |= InputString("##name", property.Path);
            ImGui::TableNextColumn();
            int kind = KindIndex(property.Initial.Kind());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##kind", &kind, kKindNames.data(), static_cast<int>(kKindNames.size())))
            {
                property.Initial = DefaultOfKind(kind);
                declarationChanged = true;
            }
            ImGui::TableNextColumn();
            declarationChanged |= EditValue(property.Initial);
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##rw", &property.Editable))
                declarationChanged = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Editable: a control may write it back");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x"))
                remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove)
    {
        model.Properties.erase(model.Properties.begin() + static_cast<std::ptrdiff_t>(*remove));
        declarationChanged = true;
    }
    if (ImGui::SmallButton("+ property"))
    {
        model.Properties.push_back(UiModelProperty{ .Path = "new_property", .Initial = UiValue(std::string{}) });
        declarationChanged = true;
    }
    if (declarationChanged)
        Redeclare();
}

void ModelPanel::DrawArrays()
{
    UiPreviewModel& model = Session.Model();
    EditorChrome::SectionTitle("Lists");
    bool declarationChanged = false;
    bool samplesChanged = false;
    std::optional<std::size_t> remove;
    for (std::size_t i = 0; i < model.Arrays.size(); ++i)
    {
        UiPreviewModel::ArraySample& list = model.Arrays[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(EditorUi::Px(140.0f));
        declarationChanged |= InputString("##name", list.Name);
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            remove = i;
        ImGui::Indent();
        std::optional<std::size_t> removeItem;
        for (std::size_t j = 0; j < list.Items.size(); ++j)
        {
            ImGui::PushID(static_cast<int>(j));
            ImGui::SetNextItemWidth(EditorUi::Px(200.0f));
            samplesChanged |= InputString("##item", list.Items[j]);
            ImGui::SameLine();
            if (ImGui::SmallButton("-"))
                removeItem = j;
            ImGui::PopID();
        }
        if (removeItem)
        {
            list.Items.erase(list.Items.begin() + static_cast<std::ptrdiff_t>(*removeItem));
            samplesChanged = true;
        }
        if (ImGui::SmallButton("+ item"))
        {
            list.Items.emplace_back("item");
            samplesChanged = true;
        }
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (remove)
    {
        model.Arrays.erase(model.Arrays.begin() + static_cast<std::ptrdiff_t>(*remove));
        declarationChanged = true;
    }
    if (ImGui::SmallButton("+ list"))
    {
        model.Arrays.push_back({ "new_list", { "first", "second" } });
        declarationChanged = true;
    }
    if (declarationChanged)
        Redeclare();
    else if (samplesChanged)
        Republish();
}

void ModelPanel::DrawRows()
{
    UiPreviewModel& model = Session.Model();
    EditorChrome::SectionTitle("Rows");
    bool declarationChanged = false;
    bool samplesChanged = false;
    std::optional<std::size_t> remove;
    for (std::size_t i = 0; i < model.Rows.size(); ++i)
    {
        UiPreviewModel::RowsSample& rows = model.Rows[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(EditorUi::Px(140.0f));
        declarationChanged |= InputString("##name", rows.Name);
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            remove = i;

        std::optional<std::size_t> removeRow;
        for (std::size_t j = 0; j < rows.Items.size(); ++j)
        {
            UiRow& row = rows.Items[j];
            ImGui::PushID(static_cast<int>(j));
            std::string header = row.Label.empty() ? "(row)" : row.Label;
            header += "###row";
            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                ImGui::SetNextItemWidth(EditorUi::Px(180.0f));
                samplesChanged |= InputString("label", row.Label);
                ImGui::SetNextItemWidth(EditorUi::Px(180.0f));
                samplesChanged |= InputString("value", row.Value);
                ImGui::SetNextItemWidth(EditorUi::Px(180.0f));
                samplesChanged |= InputString("detail", row.Detail);
                samplesChanged |= ImGui::Checkbox("editable", &row.Editable);

                int control = static_cast<int>(row.Control);
                constexpr std::array<const char*, 3> kControls{ "text", "range", "choice" };
                ImGui::SetNextItemWidth(EditorUi::Px(100.0f));
                if (ImGui::Combo("control", &control, kControls.data(), static_cast<int>(kControls.size())))
                {
                    row.Control = static_cast<UiRowControl>(control);
                    samplesChanged = true;
                }
                if (row.Control == UiRowControl::Range)
                {
                    ImGui::SetNextItemWidth(EditorUi::Px(100.0f));
                    ImGui::InputDouble("number", &row.Number, 0.0, 0.0, "%.4g");
                    samplesChanged |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SetNextItemWidth(EditorUi::Px(100.0f));
                    ImGui::InputDouble("min", &row.Min, 0.0, 0.0, "%.4g");
                    samplesChanged |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SetNextItemWidth(EditorUi::Px(100.0f));
                    ImGui::InputDouble("max", &row.Max, 0.0, 0.0, "%.4g");
                    samplesChanged |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SetNextItemWidth(EditorUi::Px(100.0f));
                    ImGui::InputDouble("step", &row.Step, 0.0, 0.0, "%.4g");
                    samplesChanged |= ImGui::IsItemDeactivatedAfterEdit();
                }
                if (row.Control == UiRowControl::Choice)
                {
                    std::string choices = JoinChoices(row.Choices);
                    ImGui::SetNextItemWidth(EditorUi::Px(220.0f));
                    if (InputString("choices", choices))
                    {
                        row.Choices = SplitChoices(choices);
                        samplesChanged = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Comma-separated labels; the value should be one of them");
                }
                if (ImGui::SmallButton("- row"))
                    removeRow = j;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeRow)
        {
            rows.Items.erase(rows.Items.begin() + static_cast<std::ptrdiff_t>(*removeRow));
            samplesChanged = true;
        }
        ImGui::Indent();
        if (ImGui::SmallButton("+ row"))
        {
            rows.Items.push_back(UiRow{ .Label = "Row", .Value = "value", .Detail = {} });
            samplesChanged = true;
        }
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (remove)
    {
        model.Rows.erase(model.Rows.begin() + static_cast<std::ptrdiff_t>(*remove));
        declarationChanged = true;
    }
    if (ImGui::SmallButton("+ rows"))
    {
        model.Rows.push_back({ "new_rows", { UiRow{ .Label = "Row", .Value = "value", .Detail = {} } } });
        declarationChanged = true;
    }
    if (declarationChanged)
        Redeclare();
    else if (samplesChanged)
        Republish();
}

void ModelPanel::DrawActions()
{
    UiPreviewModel& model = Session.Model();
    EditorChrome::SectionTitle("Actions");
    bool changed = false;
    std::optional<std::size_t> remove;
    for (std::size_t i = 0; i < model.Actions.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(EditorUi::Px(200.0f));
        changed |= InputString("##action", model.Actions[i]);
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            remove = i;
        ImGui::PopID();
    }
    if (remove)
    {
        model.Actions.erase(model.Actions.begin() + static_cast<std::ptrdiff_t>(*remove));
        changed = true;
    }
    if (ImGui::SmallButton("+ action"))
    {
        model.Actions.emplace_back("new_action");
        changed = true;
    }
    if (changed)
        Redeclare();
}

void ModelPanel::DrawBindingsTab()
{
    const UiPreviewModel& model = Session.Model();
    const std::deque<UiDiagnostic>& history = Session.Diagnostics();
    const std::vector<UiDiagnostic> snapshot(history.begin(), history.end());
    const std::vector<BindingMiss> misses = CollectBindingMisses(snapshot, model);

    ImGui::TextWrapped("What the document asked for and the model did not declare, observed so far. A binding "
                       "behind a branch not yet taken has not been observed; an action is observed when its "
                       "event fires.");
    if (misses.empty())
    {
        ImGui::TextDisabled("Nothing missing so far.");
        return;
    }

    UiPreviewModel& editable = Session.Model();
    bool declared = false;
    if (ImGui::BeginTable("misses", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(64.0f));
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("declare as", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < misses.size(); ++i)
        {
            const BindingMiss& miss = misses[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(MissKindLabel(miss.Kind));
            ImGui::TableNextColumn();
            if (!miss.Parent.empty())
                ImGui::Text("%s . %s", miss.Parent.c_str(), miss.Name.c_str());
            else
                ImGui::TextUnformatted(miss.Name.c_str());
            ImGui::TableNextColumn();
            switch (miss.Kind)
            {
            case BindingMissKind::Variable:
                if (ImGui::SmallButton("property"))
                {
                    editable.Properties.push_back(
                        UiModelProperty{ .Path = miss.Name, .Initial = UiValue(std::string{ miss.Name }) });
                    declared = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("list"))
                {
                    editable.Arrays.push_back({ miss.Name, { "first", "second" } });
                    declared = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("rows"))
                {
                    editable.Rows.push_back({ miss.Name, { UiRow{ .Label = "Row", .Value = "value", .Detail = {} } } });
                    declared = true;
                }
                break;
            case BindingMissKind::Member:
                if (!miss.Parent.empty() && !model.DeclaresRows(miss.Parent) && !model.DeclaresArray(miss.Parent))
                {
                    if (ImGui::SmallButton("rows"))
                    {
                        editable.Rows.push_back({ miss.Parent, { UiRow{ .Label = "Row", .Value = "value", .Detail = {} } } });
                        declared = true;
                    }
                }
                else
                {
                    // A row's shape is the engine's, not the sidecar's: the
                    // document names a member a row does not have.
                    ImGui::TextWrapped("not a row member (label, value, detail, editable, control, number, min, "
                                       "max, step, choices)");
                }
                break;
            case BindingMissKind::Action:
                if (ImGui::SmallButton("action"))
                {
                    editable.Actions.push_back(miss.Name);
                    declared = true;
                }
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (declared)
        Redeclare();
}

void ModelPanel::Redeclare()
{
    // The remade screen reports again what is still missing; the history is
    // kept so the row that led here does not vanish under the click.
    (void)Session.Reopen();
}

void ModelPanel::Republish()
{
    Session.Model().Publish(Session.Service(), Session.OpenScreen());
}
