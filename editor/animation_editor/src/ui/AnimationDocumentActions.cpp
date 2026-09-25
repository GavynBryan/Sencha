#include "ui/AnimationDocumentActions.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/EditorUiFeature.h"

#include <app/Engine.h>
#include <imgui.h>

#include <algorithm>

namespace
{
bool AnyUnsaved(const AnimationPreviewWorkspace& workspace)
{
    return std::ranges::any_of(workspace.Documents, [](const auto& document) { return document->IsDirty(); })
        || std::ranges::any_of(workspace.ClipEventDocuments, [](const auto& document) { return document->IsDirty(); });
}
}

void ConfigureAnimationDocumentActions(EditorUiFeature& ui, Engine& engine,
                                       AnimationPreviewWorkspace& workspace)
{
    ui.SetUndoActions([&workspace] { workspace.Undo(); }, [&workspace] { workspace.Redo(); },
                      [&workspace] { return workspace.CanUndo(); }, [&workspace] { return workspace.CanRedo(); });
    ui.SetFileActions({}, {}, [&workspace] {
        if (auto* doc = workspace.ActiveDocumentAny()) workspace.SaveDocument(*doc);
    }, {});
    ui.SetSaveAllAction([&workspace] { (void)workspace.SaveAll(); });
    engine.OnExitRequested = [&workspace](Engine::ExitSource) {
        if (auto* doc = workspace.ActiveDocumentAny()) doc->CommitEdit();
        return AnyUnsaved(workspace) ? Engine::ExitDecision::Defer : Engine::ExitDecision::Allow;
    };
    ui.AddOverlay([&engine, &workspace] {
        if (!engine.IsExitPending()) return;
        constexpr const char* title = "Unsaved animation documents";
        if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Save authored changes before closing?");
            for (const auto& document : workspace.Documents)
                if (document->IsDirty()) ImGui::BulletText("%s", document->VirtualPath().c_str());
            for (const auto& document : workspace.ClipEventDocuments)
                if (document->IsDirty()) ImGui::BulletText("%s events", document->ClipPath().c_str());
            for (const std::string& path : workspace.LastSave.Conflicts)
                ImGui::TextWrapped("%s changed on disk: keep yours or take the file's under Changes.", path.c_str());
            for (const auto& [path, why] : workspace.LastSave.Failed)
                ImGui::TextWrapped("%s was not saved: %s", path.c_str(), why.c_str());
            if (ImGui::Button("Save all and close"))
            {
                const AnimationSaveReport report = workspace.SaveAll();
                if (report.Conflicts.empty() && report.Failed.empty())
                {
                    ImGui::CloseCurrentPopup();
                    engine.ConfirmExit();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard and close"))
            {
                ImGui::CloseCurrentPopup();
                engine.ConfirmExit();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep editing") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                ImGui::CloseCurrentPopup();
                engine.CancelExit();
            }
            ImGui::EndPopup();
        }
    });
}
