#include "ui/AnimationDocumentPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/DataDocumentTabs.h"
#include "ui/NewDataAssetForm.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <memory>
#include <string>

namespace
{
class DocumentFormPanel final : public IEditorPanel
{
public:
    explicit DocumentFormPanel(AnimationPreviewWorkspace& workspace)
        : Workspace(workspace)
        , Tabs(workspace.Documents)
    {
    }
    std::string_view GetTitle() const override { return "Document"; }
    PanelPersistence GetPersistence() const override { return { "animation.document" }; }
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        if (ImGui::CollapsingHeader("New asset"))
        {
            NewAsset.Draw(Workspace.Documents, "animation/character/upper.selector.sdata");
            ImGui::TextDisabled("Reference it from the rig with Pick on the field that names it.");
            ImGui::Separator();
        }
        if (Workspace.Documents.Documents().empty())
            ImGui::TextWrapped("Open an animation asset from Preview content, or follow an \"Edit\" link, to "
                               "edit every field of it here.");
        Tabs.Draw([this](DataDocument& document) { DrawHeader(document); });
    }

private:
    void DrawHeader(DataDocument& document)
    {
        ImGui::TextDisabled("%s%s", document.Subtype().c_str(), document.IsDirty() ? ", unsaved" : "");
        if (ImGui::Button("Save"))
            (void)Workspace.SaveDocument(Workspace.Documents.RefOf(document));
        if (!Workspace.DocumentError.empty())
            ImGui::TextWrapped("%s", Workspace.DocumentError.c_str());
        if (const std::string status = Workspace.PreviewStatusOf(document); !status.empty())
            ImGui::TextWrapped("%s", status.c_str());
        for (const DataValidationError& error : document.ValidationErrors())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s: %s", error.Path.c_str(), error.Message.c_str());
        ImGui::Separator();
    }

    AnimationPreviewWorkspace& Workspace;
    DataDocumentTabs Tabs;
    NewDataAssetForm NewAsset;
};
}

void AddAnimationDocumentPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<DocumentFormPanel>(workspace));
}
