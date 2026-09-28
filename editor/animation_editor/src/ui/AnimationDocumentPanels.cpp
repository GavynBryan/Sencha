#include "ui/AnimationDocumentPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/DataForm.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <core/metadata/DataSchema.h>

#include <imgui.h>

#include <array>
#include <memory>
#include <span>
#include <string>

namespace
{
class DocumentFormPanel final : public IEditorPanel
{
public:
    explicit DocumentFormPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Document"; }
    PanelPersistence GetPersistence() const override { return { "animation.document" }; }
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        DrawNewAsset();
        DataDocument* document = Workspace.Documents.Active();
        if (document == nullptr)
        {
            ImGui::TextWrapped("Open an animation asset from Preview content, or follow an \"Edit\" link, to "
                               "edit every field of it here.");
            return;
        }
        ImGui::TextUnformatted(document->VirtualPath().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", document->Subtype().c_str(), document->IsDirty() ? ", unsaved" : "");
        if (ImGui::Button("Save"))
            (void)Workspace.SaveDocument(Workspace.Documents.RefOf(*document));
        if (!Workspace.DocumentError.empty())
            ImGui::TextWrapped("%s", Workspace.DocumentError.c_str());
        if (const std::string status = Workspace.PreviewStatusOf(*document); !status.empty())
            ImGui::TextWrapped("%s", status.c_str());
        for (const DataValidationError& error : document->ValidationErrors())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s: %s", error.Path.c_str(), error.Message.c_str());
        ImGui::Separator();

        const DataSchema* schema = Workspace.Documents.SchemaOf(*document);
        JsonValue root = document->CopyRoot();
        JsonValue* data = root.Find("data");
        if (schema == nullptr || data == nullptr)
        {
            ImGui::TextWrapped("No authoring schema is registered for '%s'.", document->Subtype().c_str());
            return;
        }
        // Escape abandons an interaction wherever it started.
        if (document->IsEditing() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            Workspace.Documents.CancelEdit(*document);
            return;
        }
        ImGui::PushID(document->VirtualPath().c_str());
        const FieldEdit edit = DrawDataField(*data, schema->Root, "$.data", Workspace.Documents);
        ApplyFieldEdit(*document, Workspace.Documents, edit, std::move(root));
        ImGui::PopID();
    }

private:
    void DrawNewAsset()
    {
        if (!ImGui::CollapsingHeader("New asset"))
            return;
        const std::span<const std::string_view> subtypes = AnimationDocumentSubtypes();
        const std::string current(subtypes[NewSubtype]);
        if (ImGui::BeginCombo("Kind", current.c_str()))
        {
            for (std::size_t i = 0; i < subtypes.size(); ++i)
                if (ImGui::Selectable(std::string(subtypes[i]).c_str(), i == NewSubtype))
                    NewSubtype = i;
            ImGui::EndCombo();
        }
        ImGui::InputTextWithHint("Path", "animation/character/upper.selector.sdata", NewPath.data(), NewPath.size());
        if (ImGui::Button("Create"))
        {
            NewError.clear();
            if ((Workspace.Documents.Create(subtypes[NewSubtype], NewPath.data(), NewError) != nullptr))
                NewPath[0] = '\0';
        }
        if (!NewError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", NewError.c_str());
        ImGui::TextDisabled("Reference it from the rig with Pick on the field that names it.");
        ImGui::Separator();
    }

    AnimationPreviewWorkspace& Workspace;
    std::size_t NewSubtype = 0;
    std::array<char, 256> NewPath{};
    std::string NewError;
};
}

void AddAnimationDocumentPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<DocumentFormPanel>(workspace));
}
