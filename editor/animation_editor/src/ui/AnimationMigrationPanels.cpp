#include "ui/AnimationMigrationPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <memory>
#include <string>

namespace
{
class MigrationPanel final : public IEditorPanel
{
public:
    explicit MigrationPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Migration"; }
    PanelPersistence GetPersistence() const override { return { "animation.migration" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        if (!Scanned)
        {
            Workspace.ScanClipPlayers();
            Scanned = true;
        }
        ImGui::TextWrapped("Scenes may still name a clip directly on an AnimationClipPlayer, which no longer "
                           "exists: such an entity loads unposed. Converting gives each a one-layer rig that plays "
                           "the clip as the player did -- same time, speed, and loop or clamp -- under "
                           "animation/migrated/, declares the rigs' names, and rewrites the scenes.");
        if (ImGui::Button("Scan project"))
        {
            Workspace.ScanClipPlayers();
            Status.clear();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(Workspace.ClipPlayerUses.empty());
        if (ImGui::Button("Convert to rigs"))
            Status = Workspace.MigrateClipPlayers(Error) ? "Converted." : Error;
        ImGui::EndDisabled();
        if (!Status.empty())
            ImGui::TextWrapped("%s", Status.c_str());
        for (const std::string& problem : Workspace.ClipPlayerProblems)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", problem.c_str());

        if (Workspace.ClipPlayerUses.empty())
        {
            ImGui::TextDisabled("No scene in the project names a clip player.");
            return;
        }
        if (!ImGui::BeginTable("uses", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
            return;
        ImGui::TableSetupColumn("Scene");
        ImGui::TableSetupColumn("Entity");
        ImGui::TableSetupColumn("Clip");
        ImGui::TableSetupColumn("Time / speed");
        ImGui::TableSetupColumn("Loop");
        ImGui::TableHeadersRow();
        for (const AnimationClipPlayerUse& use : Workspace.ClipPlayerUses)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(use.Scene.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(use.Entity.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(use.Clip.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.2fs at %.2fx", use.TimeSeconds, use.Rate);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(use.Loop ? "loop" : "clamp");
        }
        ImGui::EndTable();
    }

private:
    AnimationPreviewWorkspace& Workspace;
    bool Scanned = false;
    std::string Status;
    std::string Error;
};
}

void AddAnimationMigrationPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<MigrationPanel>(workspace));
}
