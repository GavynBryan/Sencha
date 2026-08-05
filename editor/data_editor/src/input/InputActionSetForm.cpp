#include "input/InputActionSetForm.h"

#include "DataEditorWorkspace.h"
#include "JsonObjectEdit.h"
#include "input/InputBindingSummary.h"

#include "ui/ButtonFlow.h"

#include <core/metadata/DataSchema.h>

#include <imgui.h>

#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <string>

namespace
{
constexpr std::string_view kSubtype = "input.actions";

// The schema owns the designer wording for every enum, so the form reads it
// back rather than restating it and letting the two drift.
const DataFieldSchema* ActionRecordSchema(const DataSchema& schema)
{
    const DataFieldSchema* actions = FindChild(schema.Root, "actions");
    if (actions == nullptr || actions->Children.empty())
        return nullptr;
    return &actions->Children.front();
}

std::string EnumDisplayName(const DataFieldSchema* record,
                            std::string_view key,
                            std::string_view value,
                            std::string_view fallback)
{
    if (record == nullptr)
        return std::string(fallback);
    const DataFieldSchema* field = FindChild(*record, key);
    if (field == nullptr)
        return std::string(fallback);
    for (const DataEnumChoice& choice : field->EnumChoices)
    {
        if (choice.Value == value)
            return choice.DisplayName;
    }
    return std::string(fallback);
}

// A combo over a schema enum, defaulting to the schema's own default when the
// document has not said otherwise.
FieldEdit DrawEnumRow(JsonValue& owner,
                      const DataFieldSchema* record,
                      std::string_view key,
                      const char* label,
                      std::string_view fallbackValue)
{
    FieldEdit edit;
    if (record == nullptr)
        return edit;
    const DataFieldSchema* field = FindChild(*record, key);
    if (field == nullptr || field->EnumChoices.empty())
        return edit;

    std::string current = ReadMemberString(owner, key);
    if (current.empty())
        current = std::string(fallbackValue);

    const std::string preview =
        EnumDisplayName(record, key, current, current);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
    if (ImGui::BeginCombo(label, preview.c_str()))
    {
        for (const DataEnumChoice& choice : field->EnumChoices)
        {
            const bool selected = choice.Value == current;
            if (ImGui::Selectable(choice.DisplayName.c_str(), selected))
            {
                SetMember(owner, key, JsonValue(choice.Value));
                edit |= FieldEdit::Instant();
            }
            if (ImGui::IsItemHovered() && !choice.Description.empty())
                ImGui::SetTooltip("%s", choice.Description.c_str());
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered() && !field->Summary.empty())
        ImGui::SetTooltip("%s", field->Summary.c_str());
    return edit;
}

FieldEdit DrawNameRow(JsonValue& action)
{
    std::array<char, 256> buffer{};
    const std::string current = ReadMemberString(action, "name");
    const std::size_t count = std::min(current.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), current.c_str(), count);

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
    const bool edited = ImGui::InputText("Name", buffer.data(), buffer.size());
    if (edited)
        SetMember(action, "name", JsonValue(std::string(buffer.data())));
    ImGui::SetItemTooltip("Bindings and gameplay both refer to this action by this name.");
    return FieldEdit{ edited, ImGui::IsItemDeactivatedAfterEdit() };
}

// "move - Plane", "pause - Button, presentation".
std::string ActionCardTitle(const JsonValue& action,
                            const DataFieldSchema* record,
                            std::size_t index)
{
    const std::string name = ReadMemberString(action, "name");
    std::string title = name.empty() ? std::format("Action {}", index + 1) : name;

    std::string type = ReadMemberString(action, "type");
    if (type.empty())
        type = "digital";
    title += " - " + EnumDisplayName(record, "type", type, type);

    if (ReadMemberString(action, "scope") == "presentation")
        title += ", presentation";
    return title;
}
}

std::string_view InputActionSetSubtype()
{
    return kSubtype;
}

FieldEdit DrawInputActionSetForm(JsonValue& data,
                                 const DataSchema& schema,
                                 DataEditorWorkspace& workspace)
{
    (void)workspace;
    FieldEdit edit;

    const DataFieldSchema* record = ActionRecordSchema(schema);
    JsonValue* actionsValue = FindMember(data, "actions");
    if (actionsValue == nullptr || !actionsValue->IsArray())
    {
        SetMember(data, "actions", JsonValue(JsonValue::Array{}));
        actionsValue = FindMember(data, "actions");
        edit |= FieldEdit::Instant();
    }

    JsonValue::Array& actions = actionsValue->AsArray();
    if (actions.empty())
        ImGui::TextDisabled("No actions yet. Every binding names one of these.");

    std::optional<std::size_t> remove;
    for (std::size_t index = 0; index < actions.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        const std::string title = ActionCardTitle(actions[index], record, index);
        if (ImGui::TreeNodeEx("##action", ImGuiTreeNodeFlags_AllowOverlap, "%s", title.c_str()))
        {
            ImGui::Indent();
            edit |= DrawNameRow(actions[index]);
            edit |= DrawEnumRow(actions[index], record, "type", "Value", "digital");
            edit |= DrawEnumRow(actions[index], record, "scope", "Scope", "simulation");

            ButtonFlow verbs;
            if (verbs.Button("Duplicate"))
            {
                actions.insert(actions.begin() + static_cast<std::ptrdiff_t>(index + 1),
                               actions[index]);
                edit |= FieldEdit::Instant();
            }
            if (verbs.Button("Delete"))
                remove = index;
            if (index > 0 && verbs.Button("Move up"))
            {
                std::swap(actions[index], actions[index - 1]);
                edit |= FieldEdit::Instant();
            }
            if (index + 1 < actions.size() && verbs.Button("Move down"))
            {
                std::swap(actions[index], actions[index + 1]);
                edit |= FieldEdit::Instant();
            }
            ImGui::Unindent();
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
        if (remove)
            break;
    }

    if (remove)
    {
        actions.erase(actions.begin() + static_cast<std::ptrdiff_t>(*remove));
        edit |= FieldEdit::Instant();
    }

    if (ImGui::Button("Add action"))
    {
        JsonValue::Object action;
        action.emplace_back("name", JsonValue(std::string{}));
        action.emplace_back("type", JsonValue(std::string("digital")));
        actions.push_back(JsonValue(std::move(action)));
        edit |= FieldEdit::Instant();
    }
    ImGui::SetItemTooltip("Adding an action here is what lets a profile bind a control to it.");
    return edit;
}
