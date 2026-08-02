#include "DataEditorPanels.h"

#include "DataEditorWorkspace.h"

#include <core/json/JsonFormat.h>
#include <core/json/JsonParser.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace
{
    void CopyToBuffer(std::string_view value, char* buffer, std::size_t size)
    {
        if (size == 0)
            return;
        const std::size_t count = std::min(value.size(), size - 1);
        std::memcpy(buffer, value.data(), count);
        buffer[count] = '\0';
    }

    std::string DisplayName(const DataFieldSchema& field)
    {
        return field.DisplayName.empty() ? field.Key : field.DisplayName;
    }

    void DrawFieldHelp(DataEditorWorkspace& workspace,
                       const DataFieldSchema& field,
                       std::string_view path)
    {
        if (ImGui::IsItemHovered())
        {
            workspace.SelectField(&field, std::string(path));
            if (!field.Summary.empty())
                ImGui::SetTooltip("%s", field.Summary.c_str());
        }
        if (ImGui::IsItemClicked())
            workspace.SelectField(&field, std::string(path));
    }

    bool DrawField(JsonValue& value,
                   const DataFieldSchema& field,
                   const std::string& path,
                   DataEditorWorkspace& workspace);

    bool DrawRecord(JsonValue& value,
                    const DataFieldSchema& field,
                    const std::string& path,
                    DataEditorWorkspace& workspace)
    {
        if (!value.IsObject())
            value = JsonValue(JsonValue::Object{});

        bool changed = false;
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
                    changed = true;
                }
                else
                {
                    ImGui::PushID(childPath.c_str());
                    if (ImGui::Button(("Add " + DisplayName(child)).c_str()))
                    {
                        value.AsObject().emplace_back(child.Key, CreateDefaultDataValue(child));
                        changed = true;
                    }
                    DrawFieldHelp(workspace, child, childPath);
                    ImGui::PopID();
                    continue;
                }
            }

            ImGui::PushID(childPath.c_str());
            changed = DrawField(*childValue, child, childPath, workspace) || changed;
            if (!child.Required)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    auto& object = value.AsObject();
                    object.erase(std::remove_if(object.begin(), object.end(),
                        [&child](const auto& item) { return item.first == child.Key; }),
                        object.end());
                    changed = true;
                }
            }
            ImGui::PopID();
        }
        return changed;
    }

    bool DrawArray(JsonValue& value,
                   const DataFieldSchema& field,
                   const std::string& path,
                   DataEditorWorkspace& workspace)
    {
        if (!value.IsArray())
            value = JsonValue(JsonValue::Array{});
        if (field.Children.size() != 1)
        {
            ImGui::TextUnformatted("Array schema is invalid.");
            return false;
        }

        bool changed = false;
        JsonValue::Array& array = value.AsArray();
        std::optional<std::size_t> remove;
        for (std::size_t index = 0; index < array.size(); ++index)
        {
            const std::string elementPath = std::format("{}[{}]", path, index);
            ImGui::PushID(static_cast<int>(index));
            const std::string label = std::format("Element {}", index);
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                changed = DrawField(array[index], field.Children.front(), elementPath,
                                    workspace) || changed;
                if (ImGui::SmallButton("Duplicate"))
                {
                    array.insert(array.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                 array[index]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                    remove = index;
                ImGui::SameLine();
                if (index > 0 && ImGui::SmallButton("Up"))
                {
                    std::swap(array[index], array[index - 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (index + 1 < array.size() && ImGui::SmallButton("Down"))
                {
                    std::swap(array[index], array[index + 1]);
                    changed = true;
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
            changed = true;
        }
        if (ImGui::Button("Add element"))
        {
            array.push_back(CreateDefaultDataValue(field.Children.front()));
            changed = true;
        }
        DrawFieldHelp(workspace, field, path);
        return changed;
    }

    bool DrawField(JsonValue& value,
                   const DataFieldSchema& field,
                   const std::string& path,
                   DataEditorWorkspace& workspace)
    {
        const std::string label = DisplayName(field);
        bool changed = false;

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
                changed = true;
            }
            DrawFieldHelp(workspace, field, path);
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
            if (ImGui::DragScalar(label.c_str(), ImGuiDataType_Double, &current,
                                  static_cast<float>(speed), minimum, maximum,
                                  field.Kind == DataFieldKind::Int ? "%.0f" : "%.3f"))
            {
                if (field.Kind == DataFieldKind::Int)
                    current = std::round(current);
                value = JsonValue(current);
                changed = true;
            }
            DrawFieldHelp(workspace, field, path);
            break;
        }
        case DataFieldKind::String:
        case DataFieldKind::AssetRef:
        case DataFieldKind::DataAssetRef:
        case DataFieldKind::GameplayTag:
        {
            std::array<char, 2048> buffer{};
            if (value.IsString())
                CopyToBuffer(value.AsString(), buffer.data(), buffer.size());
            const ImGuiInputTextFlags flags = field.Editor.Multiline
                ? ImGuiInputTextFlags_AllowTabInput : ImGuiInputTextFlags_None;
            const bool edited = field.Editor.Multiline
                ? ImGui::InputTextMultiline(label.c_str(), buffer.data(), buffer.size(),
                                            ImVec2(-1.0f, 90.0f), flags)
                : ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), flags);
            if (edited)
            {
                value = JsonValue(std::string(buffer.data()));
                changed = true;
            }
            DrawFieldHelp(workspace, field, path);
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
                        changed = true;
                    }
                    if (ImGui::IsItemHovered() && !choice.Description.empty())
                        ImGui::SetTooltip("%s", choice.Description.c_str());
                }
                ImGui::EndCombo();
            }
            DrawFieldHelp(workspace, field, path);
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
            if (ImGui::DragScalarN(label.c_str(), ImGuiDataType_Double, components.data(),
                                   count, 0.05f))
            {
                JsonValue::Array array;
                for (int index = 0; index < count; ++index)
                    array.emplace_back(components[index]);
                value = JsonValue(std::move(array));
                changed = true;
            }
            DrawFieldHelp(workspace, field, path);
            break;
        }
        case DataFieldKind::Record:
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawFieldHelp(workspace, field, path);
                changed = DrawRecord(value, field, path, workspace);
                ImGui::TreePop();
            }
            break;
        case DataFieldKind::Array:
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawFieldHelp(workspace, field, path);
                changed = DrawArray(value, field, path, workspace);
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
                changed = true;
            }
            DrawFieldHelp(workspace, field, path);
            if (enabled && field.Children.size() == 1)
                changed = DrawField(value, field.Children.front(), path, workspace) || changed;
            break;
        }
        }

        if (field.ReadOnly)
            ImGui::EndDisabled();
        return changed;
    }

    std::string DefaultText(const DataDefaultValue& value)
    {
        return std::visit([](const auto& item) -> std::string
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return "<none>";
            else if constexpr (std::is_same_v<T, bool>)
                return item ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::string>)
                return item;
            else
                return std::to_string(item);
        }, value);
    }
}

DataAssetBrowserPanel::DataAssetBrowserPanel(DataEditorWorkspace& workspace)
    : Workspace(workspace)
{
}

void DataAssetBrowserPanel::OnDraw()
{
    const auto types = Workspace.DataTypes();
    if (!types.empty())
    {
        SelectedSubtype = std::clamp(SelectedSubtype, 0, static_cast<int>(types.size() - 1));
        if (ImGui::BeginCombo("Type", types[SelectedSubtype].Name.c_str()))
        {
            for (int index = 0; index < static_cast<int>(types.size()); ++index)
            {
                if (ImGui::Selectable(types[index].Name.c_str(), index == SelectedSubtype))
                    SelectedSubtype = index;
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Path", NewPath.data(), NewPath.size());
        if (ImGui::Button("Create") && NewPath[0] != '\0')
        {
            LastError.clear();
            if (Workspace.Create(types[SelectedSubtype].Name, NewPath.data(), &LastError))
                NewPath.fill('\0');
        }
    }
    else
    {
        ImGui::TextWrapped("No structured data subtypes are registered by this project.");
    }

    ImGui::Separator();
    for (const AssetRecord* record : Workspace.DataAssets())
    {
        const bool selected = record->Path == SelectedAsset;
        if (ImGui::Selectable(record->Path.c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick))
        {
            SelectedAsset = record->Path;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                LastError.clear();
                (void)Workspace.Open(record->Path, &LastError);
            }
        }
    }

    if (!SelectedAsset.empty())
    {
        if (ImGui::Button("Open"))
        {
            LastError.clear();
            (void)Workspace.Open(SelectedAsset, &LastError);
        }
        ImGui::InputText("New path", OperationPath.data(), OperationPath.size());
        if (ImGui::Button("Duplicate") && OperationPath[0] != '\0')
        {
            LastError.clear();
            if (Workspace.Duplicate(SelectedAsset, OperationPath.data(), &LastError))
                OperationPath.fill('\0');
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename") && OperationPath[0] != '\0')
        {
            LastError.clear();
            if (Workspace.Rename(SelectedAsset, OperationPath.data(), &LastError))
            {
                SelectedAsset = Workspace.MakeVirtualPath(OperationPath.data());
                OperationPath.fill('\0');
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            LastError.clear();
            if (Workspace.Delete(SelectedAsset, &LastError))
                SelectedAsset.clear();
        }
    }

    if (!LastError.empty())
        ImGui::TextWrapped("Error: %s", LastError.c_str());
}

DataFormPanel::DataFormPanel(DataEditorWorkspace& workspace)
    : Workspace(workspace)
{
}

void DataFormPanel::OnDraw()
{
    auto& documents = Workspace.Documents();
    if (documents.empty())
    {
        ImGui::TextWrapped("Open or create a .sdata asset from the browser.");
        return;
    }

    if (ImGui::BeginTabBar("DataDocuments", ImGuiTabBarFlags_Reorderable))
    {
        std::optional<std::size_t> close;
        for (std::size_t index = 0; index < documents.size(); ++index)
        {
            DataDocument& document = *documents[index];
            bool open = true;
            std::string title = std::filesystem::path(document.VirtualPath()).filename().string();
            if (document.IsDirty())
                title += " *";
            title += "##" + std::to_string(index);

            const ImGuiTabItemFlags flags = Workspace.ActiveIndex() == index
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(title.c_str(), &open, flags))
            {
                Workspace.SetActive(index);
                const DataSchema* schema = Workspace.ActiveSchema();
                JsonValue root = document.CopyRoot();
                JsonValue* data = root.Find("data");
                if (schema == nullptr || data == nullptr)
                {
                    ImGui::TextWrapped("No authoring schema is registered for subtype '%s'.",
                                       document.Subtype().c_str());
                }
                else if (DrawField(*data, schema->Root, "$.data", Workspace))
                {
                    document.ReplaceRoot(std::move(root));
                    Workspace.ValidateActive();
                }
                ImGui::EndTabItem();
            }
            if (!open)
                close = index;
        }
        if (close)
            Workspace.Close(*close);
        ImGui::EndTabBar();
    }
}

DataDocumentationPanel::DataDocumentationPanel(DataEditorWorkspace& workspace)
    : Workspace(workspace)
{
}

void DataDocumentationPanel::OnDraw()
{
    const DataFieldSchema* field = Workspace.SelectedField();
    if (field == nullptr)
    {
        if (const DataSchema* schema = Workspace.ActiveSchema())
        {
            ImGui::TextUnformatted(schema->DisplayName.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s", schema->Description.c_str());
        }
        else
        {
            ImGui::TextWrapped("Select a field to see its documentation.");
        }
        return;
    }

    ImGui::TextUnformatted(DisplayName(*field).c_str());
    ImGui::TextDisabled("%s", Workspace.SelectedPath().c_str());
    ImGui::Separator();
    if (!field->Summary.empty())
        ImGui::TextWrapped("%s", field->Summary.c_str());
    if (!field->Description.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", field->Description.c_str());
    }
    if (!field->Units.empty())
        ImGui::Text("Units: %s", field->Units.c_str());
    ImGui::Text("Default: %s", DefaultText(field->Default).c_str());
    if (field->Numeric.Minimum)
        ImGui::Text("Minimum: %g", *field->Numeric.Minimum);
    if (field->Numeric.Maximum)
        ImGui::Text("Maximum: %g", *field->Numeric.Maximum);
    if (field->Numeric.Step)
        ImGui::Text("Step: %g", *field->Numeric.Step);
    if (field->Advanced)
        ImGui::TextDisabled("Advanced field");
    if (field->Deprecated)
        ImGui::TextDisabled("Deprecated field");
}

DataValidationPanel::DataValidationPanel(DataEditorWorkspace& workspace)
    : Workspace(workspace)
{
}

void DataValidationPanel::OnDraw()
{
    DataDocument* document = Workspace.Active();
    if (document == nullptr)
    {
        ImGui::TextUnformatted("No open document.");
        return;
    }

    if (document->IsExternallyModified())
        ImGui::TextWrapped("The file changed outside the editor. Reload or save explicitly to resolve the conflict.");

    const auto& errors = document->ValidationErrors();
    if (errors.empty())
    {
        ImGui::TextUnformatted("Valid. A save can hot reload the resident value.");
        return;
    }

    ImGui::Text("%zu validation error(s)", errors.size());
    for (const DataValidationError& error : errors)
    {
        ImGui::BulletText("%s: %s", error.Path.c_str(), error.Message.c_str());
    }
}

DataRawJsonPanel::DataRawJsonPanel(DataEditorWorkspace& workspace)
    : Workspace(workspace)
{
}

void DataRawJsonPanel::Refresh()
{
    const DataDocument* document = Workspace.Active();
    Buffer.fill('\0');
    if (document == nullptr)
    {
        LoadedPath.clear();
        return;
    }

    LoadedPath = document->VirtualPath();
    CopyToBuffer(JsonFormat(document->Root()), Buffer.data(), Buffer.size());
    ParseError.clear();
}

void DataRawJsonPanel::OnDraw()
{
    DataDocument* document = Workspace.Active();
    if (document == nullptr)
    {
        ImGui::TextUnformatted("No open document.");
        return;
    }
    if (LoadedPath != document->VirtualPath())
        Refresh();

    ImGui::InputTextMultiline("##raw_json", Buffer.data(), Buffer.size(),
                              ImVec2(-1.0f, -ImGui::GetFrameHeightWithSpacing() * 1.5f),
                              ImGuiInputTextFlags_AllowTabInput);
    if (ImGui::Button("Apply JSON"))
    {
        JsonParseError error;
        std::optional<JsonValue> parsed = JsonParse(Buffer.data(), &error);
        if (!parsed)
        {
            ParseError = std::format("Parse error at {}: {}", error.Position, error.Message);
        }
        else
        {
            document->ReplaceRoot(std::move(*parsed));
            Workspace.ValidateActive();
            Refresh();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset from document"))
        Refresh();
    if (!ParseError.empty())
        ImGui::TextWrapped("%s", ParseError.c_str());
}
