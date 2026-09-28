#include "DataEditorPanels.h"

#include "DataEditorWorkspace.h"
#include "SubtypeEditorRegistry.h"

#include "data/DataAssetFiles.h"
#include "ui/ButtonFlow.h"
#include "ui/DataForm.h"
#include "ui/DocumentSaveReportView.h"
#include "ui/ScopedPanel.h"
#include "ui/TextBuffer.h"

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
    bool HasChanges(DataDocumentSet& documents, std::string_view virtualPath)
    {
        const DataDocument* document = documents.Find(virtualPath);
        return document != nullptr && (document->IsDirty() || document->IsEditing());
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
            if (Workspace.Documents.Create(types[SelectedSubtype].Name, NewPath.data(), LastError) != nullptr)
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
                (void)Workspace.Documents.OpenOrFocus(record->Path, LastError);
            }
        }
    }

    if (!SelectedAsset.empty())
    {
        if (ImGui::Button("Open"))
        {
            LastError.clear();
            (void)Workspace.Documents.OpenOrFocus(SelectedAsset, LastError);
        }
        ImGui::InputText("New path", OperationPath.data(), OperationPath.size());
        if (ImGui::Button("Duplicate") && OperationPath[0] != '\0')
        {
            LastError.clear();
            if (Workspace.Duplicate(SelectedAsset, OperationPath.data(), LastError))
                OperationPath.fill('\0');
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename") && OperationPath[0] != '\0')
        {
            LastError.clear();
            Prompt.Ask(HasChanges(Workspace.Documents, SelectedAsset), SelectedAsset,
                       [this, from = SelectedAsset, to = std::string(OperationPath.data())](DirtyDisposition disposition) {
                           if (Workspace.Rename(from, to, disposition, LastError))
                           {
                               SelectedAsset = DataAssetVirtualPath(to);
                               OperationPath.fill('\0');
                           }
                       });
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            LastError.clear();
            Prompt.Ask(HasChanges(Workspace.Documents, SelectedAsset), SelectedAsset,
                       [this, path = SelectedAsset](DirtyDisposition disposition) {
                           if (Workspace.Delete(path, disposition, LastError))
                               SelectedAsset.clear();
                       });
        }
    }

    if (!LastError.empty())
        ImGui::TextWrapped("Error: %s", LastError.c_str());
    Prompt.Draw();
}

DataFormPanel::DataFormPanel(DataEditorWorkspace& workspace, SubtypeEditorRegistry& editors)
    : Workspace(workspace)
    , Editors(editors)
{
}

void DataFormPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const auto documents = Workspace.Documents.Documents();
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

            const ImGuiTabItemFlags flags = Workspace.Documents.ActiveIndex() == index
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(title.c_str(), &open, flags))
            {
                Workspace.Documents.SetActive(index);
                const DataSchema* schema = Workspace.Documents.SchemaOf(document);
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
                        Workspace.Documents.CancelEdit(document);
                    }
                    else
                    {
                        // A subtype with a purpose-built editor draws through
                        // it; everything else gets the schema-generated form.
                        IDataSubtypeEditor* editor = Editors.Find(document.Subtype());
                        SubtypeFormContext ctx{ *data, *schema, document, Workspace };
                        const FieldEdit edit = editor != nullptr
                            ? editor->DrawForm(ctx)
                            : DrawDataField(*data, schema->Root, "$.data", Workspace.Documents);
                        ApplyFieldEdit(document, Workspace.Documents, edit, std::move(root));
                    }
                }
                ImGui::EndTabItem();
            }
            if (!open)
                close = index;
        }
        if (close)
        {
            const DataDocument& closing = *documents[*close];
            Prompt.Ask(HasChanges(Workspace.Documents, closing.VirtualPath()), closing.VirtualPath(),
                       [this, path = closing.VirtualPath()](DirtyDisposition disposition) {
                           if (const std::optional<std::size_t> index = Workspace.Documents.IndexOf(path))
                               (void)Workspace.Documents.Close(*index, disposition, CloseError);
                       });
        }
        ImGui::EndTabBar();
    }
    if (!CloseError.empty())
        ImGui::TextWrapped("%s", CloseError.c_str());
    Prompt.Draw();
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

    const DataFieldSchema* field = Workspace.Documents.SelectedField();
    if (field == nullptr)
    {
        if (const DataSchema* schema = Workspace.Documents.ActiveSchema())
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

    ImGui::TextUnformatted(DataFieldDisplayName(*field).c_str());
    ImGui::TextDisabled("%s", Workspace.Documents.SelectedPath().c_str());
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

    DataDocument* document = Workspace.Documents.Active();
    if (document == nullptr)
    {
        ImGui::TextUnformatted("No open document.");
        return;
    }

    // The editor has no channel to a running game, so this reports what the
    // saved file now permits rather than a confirmed reload. The authoritative
    // confirmation is the reload counter in the game's own movement panel.
    if (const DocumentSaveResult* save = Workspace.Sources.LastSave().Find(Workspace.Documents.RefOf(*document)))
    {
        if (save->Status == DocumentSaveStatus::Saved)
            ImGui::TextUnformatted("Saved. A running game hot reloads this within ~0.3 s.");
        else if (save->Status == DocumentSaveStatus::SavedWithProblems)
            ImGui::TextWrapped("Saved with validation errors. The runtime and any running game keep "
                               "the last valid version.");
        ImGui::Separator();
    }
    DrawDocumentSaveReport(Workspace.Sources, SettleError);
    if (document->IsExternallyModified())
        ImGui::TextWrapped("The file changed outside the editor. Save refuses to overwrite it; keep yours or take "
                           "the file's once a save reports the conflict.");

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
    const DataDocument* document = Workspace.Documents.Active();
    Buffer.fill('\0');
    if (document == nullptr)
    {
        LoadedPath.clear();
        LoadedRevision = 0;
        return;
    }

    LoadedPath = document->VirtualPath();
    LoadedRevision = document->Revision();
    CopyToTextBuffer(JsonFormat(document->Root()), Buffer.data(), Buffer.size());
    ParseError.clear();
}

void DataRawJsonPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    DataDocument* document = Workspace.Documents.Active();
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
            Workspace.Documents.Changed(*document);
            Refresh();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset from document"))
        Refresh();
    if (!ParseError.empty())
        ImGui::TextWrapped("%s", ParseError.c_str());
}
