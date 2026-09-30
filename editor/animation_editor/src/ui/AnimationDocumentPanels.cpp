#include "ui/AnimationDocumentPanels.h"

#include "data/DataDocumentSet.h"
#include "documents/DocumentSourceSet.h"
#include "ui/AnimationPreviewStatus.h"
#include "ui/DocumentSaveReportView.h"
#include "ui/DataDocumentTabs.h"
#include "ui/NewDataAssetForm.h"
#include "ui/WorkspaceView.h"
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
    DocumentFormPanel(DataDocumentSet& documents, DocumentSourceSet& sources)
        : Documents(documents)
        , Sources(sources)
        , Tabs(documents)
    {
    }
    std::string_view GetTitle() const override { return "Document"; }
    PanelPersistence GetPersistence() const override { return { "animation.document" }; }
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen()) return;

        if (ImGui::CollapsingHeader("New asset"))
        {
            NewAsset.Draw(Documents, "animation/character/upper.selector.sdata");
            ImGui::TextDisabled("Reference it from the rig with Pick on the field that names it.");
            ImGui::Separator();
        }
        if (Documents.Documents().empty())
            ImGui::TextWrapped("Open an animation asset from Preview content, or follow an \"Edit\" link, to "
                               "edit every field of it here.");
        Tabs.Draw([this](DataDocument& document) { DrawHeader(document); });
    }

private:
    void DrawHeader(DataDocument& document)
    {
        ImGui::TextDisabled("%s%s", document.Subtype().c_str(), document.IsDirty() ? ", unsaved" : "");
        if (ImGui::Button("Save"))
            SaveMessage = DescribeDocumentSave(Sources.Save(Documents.RefOf(document)));
        if (!SaveMessage.empty())
            ImGui::TextWrapped("%s", SaveMessage.c_str());
        if (const std::string status = AnimationPreviewStatusText(Documents.Store().ResidentStateOf(document)); !status.empty())
            ImGui::TextWrapped("%s", status.c_str());
        for (const DataValidationError& error : document.ValidationErrors())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s: %s", error.Path.c_str(), error.Message.c_str());
        ImGui::Separator();
    }

    DataDocumentSet& Documents;
    DocumentSourceSet& Sources;
    DataDocumentTabs Tabs;
    std::string SaveMessage;
    NewDataAssetForm NewAsset;
};
}

void AddAnimationDocumentPanels(WorkspaceView& ui, DataDocumentSet& documents, DocumentSourceSet& sources)
{
    ui.AddPanel(std::make_unique<DocumentFormPanel>(documents, sources));
}
