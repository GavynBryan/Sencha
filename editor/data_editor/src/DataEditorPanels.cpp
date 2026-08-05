#include "DataEditorPanels.h"

#include "DataEditorWorkspace.h"
#include "DataFormEdit.h"
#include "movement/MovementProfileForm.h"
#include "movement/MovementResolvePreview.h"

#include "ui/ButtonFlow.h"
#include "ui/ScopedPanel.h"

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

    // Units belong beside the number being tuned; sending them only to the
    // documentation pane makes the author look away to read them.
    std::string FieldLabel(const DataFieldSchema& field)
    {
        std::string label = DisplayName(field);
        if (!field.Units.empty())
            label += " (" + field.Units + ")";
        return label;
    }

    void DrawFieldHelp(DataEditorWorkspace& workspace,
                       const DataFieldSchema& field,
                       std::string_view path)
    {
        if (ImGui::IsItemHovered() && !field.Summary.empty())
            ImGui::SetTooltip("%s", field.Summary.c_str());

        // Selection follows clicks only. Driving it from hover made the
        // documentation pane chase the pointer, so it could never be read while
        // reaching for another control.
        if (ImGui::IsItemClicked())
            workspace.SelectField(&field, std::string(path));
    }

    // Continuous widgets (drags, text fields) end their interaction here.
    FieldEdit ContinuousEdit(bool changed)
    {
        return FieldEdit{ changed, ImGui::IsItemDeactivatedAfterEdit() };
    }

    FieldEdit DrawField(JsonValue& value,
                        const DataFieldSchema& field,
                        const std::string& path,
                        DataEditorWorkspace& workspace);

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
            if (ImGui::Selectable(DisplayName(child).c_str()))
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

    // A reference to another data asset: the path stays typable, but the assets
    // that would satisfy it are a click away, filtered to the subtype the schema
    // says this field accepts. Open jumps to the referenced document, which is
    // most of what an author wants after seeing the name.
    FieldEdit DrawDataAssetRef(JsonValue& value,
                               const DataFieldSchema& field,
                               const std::string& path,
                               const std::string& label,
                               DataEditorWorkspace& workspace)
    {
        FieldEdit edit;
        const std::string current = value.IsString() ? value.AsString() : std::string{};

        std::array<char, 2048> buffer{};
        CopyToBuffer(current, buffer.data(), buffer.size());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        const bool edited = ImGui::InputText(("##ref" + path).c_str(),
                                             buffer.data(), buffer.size());
        if (edited)
            value = JsonValue(std::string(buffer.data()));
        edit |= ContinuousEdit(edited);
        DrawFieldHelp(workspace, field, path);

        const std::string popup = "pickref##" + path;
        ImGui::SameLine();
        if (ImGui::Button("Pick"))
            ImGui::OpenPopup(popup.c_str());
        if (ImGui::BeginPopup(popup.c_str()))
        {
            bool anyOffered = false;
            // Enumerated only while the popup is open: the subtype of an asset
            // that is not already in a tab costs a file read to learn.
            for (const AssetRecord* record : workspace.DataAssets())
            {
                if (!field.Reference.DataSubtype.empty()
                    && workspace.DataSubtypeOf(record->Path) != field.Reference.DataSubtype)
                {
                    continue;
                }
                anyOffered = true;
                if (ImGui::Selectable(record->Path.c_str()))
                {
                    value = JsonValue(record->Path);
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
            (void)workspace.Open(current);
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextUnformatted(label.c_str());
        return edit;
    }

    FieldEdit DrawRecord(JsonValue& value,
                         const DataFieldSchema& field,
                         const std::string& path,
                         DataEditorWorkspace& workspace)
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
                    if (ImGui::Button(("Add " + DisplayName(child)).c_str()))
                    {
                        value.AsObject().emplace_back(child.Key, CreateDefaultDataValue(child));
                        edit |= FieldEdit::Instant();
                    }
                    DrawFieldHelp(workspace, child, childPath);
                    ImGui::PopID();
                    continue;
                }
            }

            ImGui::PushID(childPath.c_str());
            edit |= DrawField(*childValue, child, childPath, workspace);
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
                        DataEditorWorkspace& workspace)
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
                // A record element draws its members straight into the card:
                // going back through DrawField would open a second node named
                // after the element schema, so a named card read
                // "Contexts > gameplay > Context".
                if (element.Kind == DataFieldKind::Record)
                    edit |= DrawRecord(array[index], element, elementPath, workspace);
                else
                    edit |= DrawField(array[index], element, elementPath, workspace);

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
        DrawFieldHelp(workspace, field, path);
        return edit;
    }

    FieldEdit DrawField(JsonValue& value,
                        const DataFieldSchema& field,
                        const std::string& path,
                        DataEditorWorkspace& workspace)
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
            DrawFieldHelp(workspace, field, path);
            break;
        }
        case DataFieldKind::DataAssetRef:
            edit |= DrawDataAssetRef(value, field, path, label, workspace);
            break;
        case DataFieldKind::String:
        case DataFieldKind::AssetRef:
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
                value = JsonValue(std::string(buffer.data()));
            edit |= ContinuousEdit(edited);
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
                        edit |= FieldEdit::Instant();
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
            DrawFieldHelp(workspace, field, path);
            break;
        }
        case DataFieldKind::Record:
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawFieldHelp(workspace, field, path);
                edit |= DrawRecord(value, field, path, workspace);
                ImGui::TreePop();
            }
            break;
        case DataFieldKind::Array:
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawFieldHelp(workspace, field, path);
                edit |= DrawArray(value, field, path, workspace);
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
            DrawFieldHelp(workspace, field, path);
            if (enabled && field.Children.size() == 1)
                edit |= DrawField(value, field.Children.front(), path, workspace);
            break;
        }
        }

        if (field.ReadOnly)
            ImGui::EndDisabled();
        return edit;
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

FieldEdit DrawDataField(JsonValue& value,
                        const DataFieldSchema& field,
                        const std::string& path,
                        DataEditorWorkspace& workspace)
{
    return DrawField(value, field, path, workspace);
}

void DrawDataFieldHelp(DataEditorWorkspace& workspace,
                       const DataFieldSchema& field,
                       std::string_view path)
{
    DrawFieldHelp(workspace, field, path);
}

DataAssetBrowserPanel::DataAssetBrowserPanel(DataEditorWorkspace& workspace)
    : Workspace(workspace)
{
}

void DataAssetBrowserPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

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

DataFormPanel::DataFormPanel(DataEditorWorkspace& workspace, MovementResolvePreview& preview)
    : Workspace(workspace)
    , Preview(preview)
{
}

void DataFormPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

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
                else
                {
                    // Escape abandons an interaction wherever it started, so a
                    // drag that went somewhere unintended costs nothing.
                    if (document.IsEditing() && ImGui::IsKeyPressed(ImGuiKey_Escape))
                    {
                        document.CancelEdit();
                        Workspace.ValidateActive();
                    }
                    else
                    {
                        // Subtypes earn a purpose-built surface one at a time;
                        // everything else gets the schema-generated form.
                        const FieldEdit edit = document.Subtype() == MovementProfileSubtype()
                            ? DrawMovementProfileForm(*data, *schema, Workspace, Preview)
                            : DrawField(*data, schema->Root, "$.data", Workspace);
                        ApplyFieldEdit(document, Workspace, edit, std::move(root));
                    }
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
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

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
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    DataDocument* document = Workspace.Active();
    if (document == nullptr)
    {
        ImGui::TextUnformatted("No open document.");
        return;
    }

    // The editor has no channel to a running game, so this reports what the
    // saved file now permits rather than a confirmed reload. The authoritative
    // confirmation is the reload counter in the game's own movement panel.
    const DataSaveReport& save = Workspace.LastSaveReport();
    if (save.Saved && save.VirtualPath == document->VirtualPath())
    {
        if (save.SemanticallyValid)
        {
            ImGui::TextUnformatted("Saved. A running game hot reloads this within ~0.3 s.");
        }
        else
        {
            ImGui::TextWrapped(
                "Saved with validation errors. The runtime and any running game keep "
                "the last valid version.");
        }
        ImGui::Separator();
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
        LoadedRevision = 0;
        return;
    }

    LoadedPath = document->VirtualPath();
    LoadedRevision = document->Revision();
    CopyToBuffer(JsonFormat(document->Root()), Buffer.data(), Buffer.size());
    ParseError.clear();
}

void DataRawJsonPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    DataDocument* document = Workspace.Active();
    if (document == nullptr)
    {
        ImGui::TextUnformatted("No open document.");
        return;
    }
    if (LoadedPath != document->VirtualPath() || LoadedRevision != document->Revision())
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
