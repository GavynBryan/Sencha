#include "ui/AnimationDocumentActions.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/EditorUiFeature.h"

#include <app/Engine.h>
#include <imgui.h>

void ConfigureAnimationDocumentActions(EditorUiFeature& ui, Engine& engine,
                                       AnimationPreviewWorkspace& workspace)
{
    ui.SetUndoActions([&workspace] { workspace.Sources.Undo(); }, [&workspace] { workspace.Sources.Redo(); },
                      [&workspace] { return workspace.Sources.CanUndo(); }, [&workspace] { return workspace.Sources.CanRedo(); });
    ui.SetFileActions({}, {}, [&workspace] {
        if (auto* doc = workspace.Documents.Active()) workspace.SaveDocument(workspace.Documents.RefOf(*doc));
    }, {});
    ui.SetSaveAllAction([&workspace] { (void)workspace.Sources.SaveAll(); });
    engine.OnExitRequested = [&workspace](Engine::ExitSource) {
        return workspace.Sources.ChangedDocuments().empty() ? Engine::ExitDecision::Allow : Engine::ExitDecision::Defer;
    };
    ui.AddOverlay([&engine, &workspace] {
        if (!engine.IsExitPending()) return;
        constexpr const char* title = "Unsaved animation documents";
        if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Save authored changes before closing?");
            for (const DocumentRef& document : workspace.Sources.ChangedDocuments())
                ImGui::BulletText("%s", document.Key.c_str());
            for (const DocumentSaveResult* result : workspace.Sources.LastSave().WithStatus(DocumentSaveStatus::Conflict))
                ImGui::TextWrapped("%s changed on disk: keep yours or take the file's under Changes.",
                                   result->Document.Key.c_str());
            for (const DocumentSaveResult* result : workspace.Sources.LastSave().WithStatus(DocumentSaveStatus::Failed))
                ImGui::TextWrapped("%s was not saved: %s", result->Document.Key.c_str(), result->Error.c_str());
            if (ImGui::Button("Save all and close"))
            {
                const DocumentSaveReport& report = workspace.Sources.SaveAll();
                if (report.WithStatus(DocumentSaveStatus::Conflict).empty()
                    && report.WithStatus(DocumentSaveStatus::Failed).empty())
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
