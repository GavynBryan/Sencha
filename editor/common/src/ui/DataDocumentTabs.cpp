#include "ui/DataDocumentTabs.h"

#include "data/DataDocumentSet.h"
#include "ui/DataForm.h"
#include "ui/DataSubtypeEditorRegistry.h"

#include <core/metadata/DataSchema.h>

#include <imgui.h>

#include <filesystem>

void DataDocumentTabs::Draw(const std::function<void(DataDocument&)>& header)
{
    const auto documents = Documents.Documents();
    if (documents.empty())
        return;
    if (!ImGui::BeginTabBar("DataDocuments"))
        return;

    const std::size_t active = Documents.ActiveIndex();
    const bool selecting = Shown != active;
    std::optional<std::size_t> close;
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        DataDocument& document = *documents[index];
        bool open = true;
        const std::string title = std::filesystem::path(document.VirtualPath()).filename().string()
            + (document.IsDirty() ? " *" : "") + "###" + document.VirtualPath();
        const ImGuiTabItemFlags flags = selecting && index == active ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(title.c_str(), &open, flags))
        {
            // While the set's own choice is on its way to the tab bar, the old tab still reports selected.
            if (!selecting && index != Documents.ActiveIndex())
                Documents.SetActive(index);
            if (index == Documents.ActiveIndex())
            {
                Shown = index;
                if (header)
                    header(document);
                DrawForm(document);
            }
            ImGui::EndTabItem();
        }
        if (!open)
            close = index;
    }
    ImGui::EndTabBar();

    if (close)
    {
        const DataDocument& closing = *documents[*close];
        Prompt.Ask(closing.IsDirty() || closing.IsEditing(), closing.VirtualPath(),
                   [this, path = closing.VirtualPath()](DirtyDisposition disposition) {
                       if (const std::optional<std::size_t> index = Documents.IndexOf(path))
                           (void)Documents.Close(*index, disposition, CloseError);
                   });
    }
    if (!CloseError.empty())
        ImGui::TextWrapped("%s", CloseError.c_str());
    Prompt.Draw();
}

void DataDocumentTabs::DrawForm(DataDocument& document)
{
    const DataSchema* schema = Documents.Store().SchemaOf(document);
    JsonValue root = document.CopyRoot();
    JsonValue* data = root.Find("data");
    if (schema == nullptr || data == nullptr)
    {
        ImGui::TextWrapped("No authoring schema is registered for '%s'.", document.Subtype().c_str());
        return;
    }
    if (document.IsEditing() && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        Documents.Store().CancelEdit(document);
        return;
    }
    ImGui::PushID(document.VirtualPath().c_str());
    IDataSubtypeEditor* editor = Editors != nullptr ? Editors->Find(document.Subtype()) : nullptr;
    DataSubtypeFormContext context{ *data, *schema, document, Documents };
    const FieldEdit edit = editor != nullptr ? editor->DrawForm(context)
                                             : DrawDataField(*data, schema->Root, "$.data", Documents);
    ApplyFieldEdit(document, Documents, edit, std::move(root));
    ImGui::PopID();
}
