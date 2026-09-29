#include "ui/DataForm.h"

#include "data/DataDocument.h"
#include "ui/ButtonFlow.h"
#include "ui/TextBuffer.h"

#include <core/metadata/DataSchema.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

std::string DataFieldDisplayName(const DataFieldSchema& field)
{
    return field.DisplayName.empty() ? field.Key : field.DisplayName;
}

void ApplyFieldEdit(DataDocument& document, DataFormHost& host, const FieldEdit& edit, JsonValue root)
{
    if (edit.Changed)
    {
        document.BeginEdit();
        document.PreviewRoot(std::move(root));
        host.EditPreviewed(document);
    }

    // A commit with no change still closes a scope an earlier frame opened, so
    // it runs whether or not this frame moved anything.
    if (edit.Committed)
    {
        document.CommitEdit();
        host.EditCommitted(document);
    }
}

namespace
{
    // Units belong beside the number being tuned; sending them only to the
    // documentation pane makes the author look away to read them.
    std::string FieldLabel(const DataFieldSchema& field)
    {
        std::string label = DataFieldDisplayName(field);
        if (!field.Units.empty())
            label += " (" + field.Units + ")";
        return label;
    }

    void DrawFieldHelp(DataFormHost& host,
                       const DataFieldSchema& field,
                       std::string_view path)
    {
        if (ImGui::IsItemHovered() && !field.Summary.empty())
            ImGui::SetTooltip("%s", field.Summary.c_str());

        // Selection follows clicks only. Driving it from hover made the
        // documentation pane chase the pointer, so it could never be read while
        // reaching for another control.
        if (ImGui::IsItemClicked())
            host.SelectField(field, path);
    }

    // Continuous widgets (drags, text fields) end their interaction here.
    FieldEdit ContinuousEdit(bool changed)
    {
        return FieldEdit{ changed, ImGui::IsItemDeactivatedAfterEdit() };
    }

    FieldEdit DrawField(JsonValue& value,
                        const DataFieldSchema& field,
                        const std::string& path,
                        DataFormHost& host);

    // Absent optional members behind one popup, for records where a button per
    // member would bury the values actually set.
    FieldEdit DrawAddOptionalMember(JsonValue& value,
                                    const DataFieldSchema& field,
                                    const std::string& path)
    {
        FieldEdit edit;
        const std::string popup = "add##" + path;
        if (ImGui::Button("Add"))
            ImGui::OpenPopup(popup.c_str());
        if (!ImGui::BeginPopup(popup.c_str()))
            return edit;

        bool anyOffered = false;
        for (const DataFieldSchema& child : field.Children)
        {
            if (child.Required || value.Find(child.Key) != nullptr)
                continue;
            anyOffered = true;
            if (ImGui::Selectable(DataFieldDisplayName(child).c_str()))
            {
                value.AsObject().emplace_back(child.Key, CreateDefaultDataValue(child));
                edit |= FieldEdit::Instant();
            }
            if (ImGui::IsItemHovered() && !child.Summary.empty())
                ImGui::SetTooltip("%s", child.Summary.c_str());
        }
        if (!anyOffered)
            ImGui::TextDisabled("Everything here is already set.");
        ImGui::EndPopup();
        return edit;
    }

    // A data asset reference: typable, with Pick offering assets of the subtype
    // the schema accepts, and Open jumping to the one named.
    FieldEdit DrawDataAssetRef(JsonValue& value,
                               const DataFieldSchema& field,
                               const std::string& path,
                               const std::string& label,
                               DataFormHost& host)
    {
        FieldEdit edit;
        const std::string current = value.IsString() ? value.AsString() : std::string{};

        std::array<char, 2048> buffer{};
        CopyToTextBuffer(current, buffer.data(), buffer.size());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        const bool edited = ImGui::InputText(("##ref" + path).c_str(),
                                             buffer.data(), buffer.size());
        if (edited)
            value = JsonValue(std::string(buffer.data()));
        edit |= ContinuousEdit(edited);
        DrawFieldHelp(host, field, path);

        const std::string popup = "pickref##" + path;
        ImGui::SameLine();
        if (ImGui::Button("Pick"))
            ImGui::OpenPopup(popup.c_str());
        if (ImGui::BeginPopup(popup.c_str()))
        {
            bool anyOffered = false;
            // Enumerated only while the popup is open: the subtype of an asset
            // that is not already in a tab costs a file read to learn.
            for (const std::string& candidate : host.DataAssetPaths(field.Reference.DataSubtype))
            {
                anyOffered = true;
                if (ImGui::Selectable(candidate.c_str()))
                {
                    value = JsonValue(candidate);
                    edit |= FieldEdit::Instant();
                }
            }
            if (!anyOffered)
            {
                ImGui::TextDisabled("%s", field.Reference.DataSubtype.empty()
                    ? "This project has no data assets."
                    : ("No " + field.Reference.DataSubtype + " assets in this project.").c_str());
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(current.empty());
        if (ImGui::Button("Open"))
            host.OpenDataAsset(current);
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextUnformatted(label.c_str());
        return edit;
    }

    FieldEdit DrawRecord(JsonValue& value,
                         const DataFieldSchema& field,
                         const std::string& path,
                         DataFormHost& host)
    {
        if (!value.IsObject())
            value = JsonValue(JsonValue::Object{});

        const bool compact = field.Editor.Widget == "compact";

        FieldEdit edit;
        for (const DataFieldSchema& child : field.Children)
        {
            JsonValue* childValue = value.Find(child.Key);
            const std::string childPath = path + "." + child.Key;
            if (childValue == nullptr)
            {
                if (child.Required)
                {
                    value.AsObject().emplace_back(child.Key, CreateDefaultDataValue(child));
                    childValue = &value.AsObject().back().second;
                    edit |= FieldEdit::Instant();
                }
                else if (compact)
                {
                    continue; // offered through the popup below instead
                }
                else
                {
                    ImGui::PushID(childPath.c_str());
                    if (ImGui::Button(("Add " + DataFieldDisplayName(child)).c_str()))
                    {
                        value.AsObject().emplace_back(child.Key, CreateDefaultDataValue(child));
                        edit |= FieldEdit::Instant();
                    }
                    DrawFieldHelp(host, child, childPath);
                    ImGui::PopID();
                    continue;
                }
            }

            ImGui::PushID(childPath.c_str());
            edit |= DrawField(*childValue, child, childPath, host);
            if (!child.Required)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    auto& object = value.AsObject();
                    object.erase(std::remove_if(object.begin(), object.end(),
                        [&child](const auto& item) { return item.first == child.Key; }),
                        object.end());
                    edit |= FieldEdit::Instant();
                }
            }
            ImGui::PopID();
        }

        if (compact)
            edit |= DrawAddOptionalMember(value, field, path);
        return edit;
    }

    FieldEdit DrawArray(JsonValue& value,
                        const DataFieldSchema& field,
                        const std::string& path,
                        DataFormHost& host)
    {
        if (!value.IsArray())
            value = JsonValue(JsonValue::Array{});
        if (field.Children.size() != 1)
        {
            ImGui::TextUnformatted("Array schema is invalid.");
            return {};
        }

        FieldEdit edit;
        const DataFieldSchema& element = field.Children.front();
        const std::string elementName = element.DisplayName.empty()
            ? std::string("Element") : element.DisplayName;

        // Cards read as a list first and an editor second, so a long array can be
        // scanned without expanding every entry.
        const bool cards = field.Editor.Widget == "cards";
        const ImGuiTreeNodeFlags rowFlags =
            cards ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen;

        JsonValue::Array& array = value.AsArray();
        std::optional<std::size_t> remove;
        for (std::size_t index = 0; index < array.size(); ++index)
        {
            const std::string elementPath = std::format("{}[{}]", path, index);
            ImGui::PushID(static_cast<int>(index));
            // Counted from one and named after the element schema: the author
            // reads "Layer 2", not a zero-based array offset. A card titles
            // itself by what the author named the element instead.
            std::string label = std::format("{} {}", elementName, index + 1);
            if (cards && !field.Editor.TitleKey.empty() && array[index].IsObject())
            {
                if (const JsonValue* title = array[index].Find(field.Editor.TitleKey);
                    title != nullptr && title->IsString() && !title->AsString().empty())
                {
                    label = title->AsString();
                }
            }
            if (ImGui::TreeNodeEx(label.c_str(), rowFlags))
            {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", elementPath.c_str());
                // Straight into the card: DrawField would nest a second node named
                // after the element ("Contexts > gameplay > Context").
                if (element.Kind == DataFieldKind::Record)
                    edit |= DrawRecord(array[index], element, elementPath, host);
                else
                    edit |= DrawField(array[index], element, elementPath, host);

                ButtonFlow verbs;
                if (verbs.Button("Duplicate"))
                {
                    array.insert(array.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                 array[index]);
                    edit |= FieldEdit::Instant();
                }
                if (verbs.Button("Delete"))
                    remove = index;
                if (index > 0 && verbs.Button("Up"))
                {
                    std::swap(array[index], array[index - 1]);
                    edit |= FieldEdit::Instant();
                }
                if (index + 1 < array.size() && verbs.Button("Down"))
                {
                    std::swap(array[index], array[index + 1]);
                    edit |= FieldEdit::Instant();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            if (remove)
                break;
        }

        if (remove)
        {
            array.erase(array.begin() + static_cast<std::ptrdiff_t>(*remove));
            edit |= FieldEdit::Instant();
        }
        if (ImGui::Button(("Add " + elementName).c_str()))
        {
            array.push_back(CreateDefaultDataValue(element));
            edit |= FieldEdit::Instant();
        }
        DrawFieldHelp(host, field, path);
        return edit;
    }

    FieldEdit DrawField(JsonValue& value,
                        const DataFieldSchema& field,
                        const std::string& path,
                        DataFormHost& host)
    {
        const std::string label = FieldLabel(field);
        FieldEdit edit;

        if (field.ReadOnly)
            ImGui::BeginDisabled();

        switch (field.Kind)
        {
        case DataFieldKind::Bool:
        {
            bool current = value.IsBool() ? value.AsBool() : false;
            if (ImGui::Checkbox(label.c_str(), &current))
            {
                value = JsonValue(current);
                edit |= FieldEdit::Instant();
            }
            DrawFieldHelp(host, field, path);
            break;
        }
        case DataFieldKind::Int:
        case DataFieldKind::Float:
        {
            double current = value.IsNumber() ? value.AsNumber() : 0.0;
            const double speed = field.Numeric.Step.value_or(
                field.Kind == DataFieldKind::Int ? 1.0 : 0.05);
            const double* minimum = field.Numeric.Minimum ? &*field.Numeric.Minimum : nullptr;
            const double* maximum = field.Numeric.Maximum ? &*field.Numeric.Maximum : nullptr;
            const bool dragged = ImGui::DragScalar(
                label.c_str(), ImGuiDataType_Double, &current,
                static_cast<float>(speed), minimum, maximum,
                field.Kind == DataFieldKind::Int ? "%.0f" : "%.3f");
            if (dragged)
            {
                if (field.Kind == DataFieldKind::Int)
                    current = std::round(current);
                value = JsonValue(current);
            }
            edit |= ContinuousEdit(dragged);
            DrawFieldHelp(host, field, path);
            break;
        }
        case DataFieldKind::DataAssetRef:
            edit |= DrawDataAssetRef(value, field, path, label, host);
            break;
        case DataFieldKind::String:
        case DataFieldKind::AssetRef:
        case DataFieldKind::GameplayTag:
        // Typed as text until a surface exists that can pick an entity out of
        // a scene: the sixteen hex digits are what the file holds either way,
        // and a field the author can read and paste beats one they cannot see.
        case DataFieldKind::Entity:
        {
            std::array<char, 2048> buffer{};
            if (value.IsString())
                CopyToTextBuffer(value.AsString(), buffer.data(), buffer.size());
            const ImGuiInputTextFlags flags = field.Editor.Multiline
                ? ImGuiInputTextFlags_AllowTabInput : ImGuiInputTextFlags_None;
            const bool edited = field.Editor.Multiline
                ? ImGui::InputTextMultiline(label.c_str(), buffer.data(), buffer.size(),
                                            ImVec2(-1.0f, 90.0f), flags)
                : ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), flags);
            if (edited)
                value = JsonValue(std::string(buffer.data()));
            edit |= ContinuousEdit(edited);
            DrawFieldHelp(host, field, path);
            break;
        }
        case DataFieldKind::Enum:
        {
            const std::string current = value.IsString() ? value.AsString() : std::string{};
            if (ImGui::BeginCombo(label.c_str(), current.c_str()))
            {
                for (const DataEnumChoice& choice : field.EnumChoices)
                {
                    const bool selected = choice.Value == current;
                    const char* choiceLabel = choice.DisplayName.empty()
                        ? choice.Value.c_str() : choice.DisplayName.c_str();
                    if (ImGui::Selectable(choiceLabel, selected))
                    {
                        value = JsonValue(choice.Value);
                        edit |= FieldEdit::Instant();
                    }
                    if (ImGui::IsItemHovered() && !choice.Description.empty())
                        ImGui::SetTooltip("%s", choice.Description.c_str());
                }
                ImGui::EndCombo();
            }
            DrawFieldHelp(host, field, path);
            break;
        }
        case DataFieldKind::Vector:
        {
            std::array<double, 4> components{};
            if (value.IsArray())
            {
                for (std::size_t index = 0;
                     index < std::min<std::size_t>(value.AsArray().size(), components.size());
                     ++index)
                {
                    if (value.AsArray()[index].IsNumber())
                        components[index] = value.AsArray()[index].AsNumber();
                }
            }
            const int count = static_cast<int>(std::min<uint32_t>(field.VectorLength, 4));
            const bool dragged = ImGui::DragScalarN(
                label.c_str(), ImGuiDataType_Double, components.data(), count, 0.05f);
            if (dragged)
            {
                JsonValue::Array array;
                for (int index = 0; index < count; ++index)
                    array.emplace_back(components[index]);
                value = JsonValue(std::move(array));
            }
            edit |= ContinuousEdit(dragged);
            DrawFieldHelp(host, field, path);
            break;
        }
        case DataFieldKind::Record:
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawFieldHelp(host, field, path);
                edit |= DrawRecord(value, field, path, host);
                ImGui::TreePop();
            }
            break;
        case DataFieldKind::Array:
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawFieldHelp(host, field, path);
                edit |= DrawArray(value, field, path, host);
                ImGui::TreePop();
            }
            break;
        case DataFieldKind::Optional:
        {
            bool enabled = !value.IsNull();
            if (ImGui::Checkbox(("Enable " + label).c_str(), &enabled))
            {
                value = enabled && field.Children.size() == 1
                    ? CreateDefaultDataValue(field.Children.front()) : JsonValue();
                edit |= FieldEdit::Instant();
            }
            DrawFieldHelp(host, field, path);
            if (enabled && field.Children.size() == 1)
                edit |= DrawField(value, field.Children.front(), path, host);
            break;
        }
        }

        if (field.ReadOnly)
            ImGui::EndDisabled();
        return edit;
    }
}

FieldEdit DrawDataField(JsonValue& value,
                        const DataFieldSchema& field,
                        const std::string& path,
                        DataFormHost& host)
{
    return DrawField(value, field, path, host);
}

void DrawDataFieldHelp(DataFormHost& host,
                       const DataFieldSchema& field,
                       std::string_view path)
{
    DrawFieldHelp(host, field, path);
}
