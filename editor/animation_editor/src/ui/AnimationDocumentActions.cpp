#include "ui/AnimationDocumentActions.h"

#include "AnimationPreviewWorkspace.h"
#include "ui/EditorUiFeature.h"

#include <app/Engine.h>
#include <imgui.h>

#include <algorithm>

namespace
{
DataDocument* Active(AnimationPreviewWorkspace& workspace)
{
    return workspace.ActiveDocument < workspace.Documents.size()
        ? workspace.Documents[workspace.ActiveDocument].get() : nullptr;
}

bool SaveAll(AnimationPreviewWorkspace& workspace)
{
    for (const auto& document : workspace.Documents)
        if (document->IsDirty() && !workspace.SaveDocument(*document)) return false;
    return true;
}
}

void ConfigureAnimationDocumentActions(EditorUiFeature& ui, Engine& engine,
                                       AnimationPreviewWorkspace& workspace)
{
    ui.SetUndoActions(
        [&workspace] { if (auto* doc = Active(workspace)) { doc->Undo(); workspace.ValidateDocument(*doc); } },
        [&workspace] { if (auto* doc = Active(workspace)) { doc->Redo(); workspace.ValidateDocument(*doc); } },
        [&workspace] { const auto* doc = Active(workspace); return doc && doc->CanUndo(); },
        [&workspace] { const auto* doc = Active(workspace); return doc && doc->CanRedo(); });
    ui.SetFileActions({}, {}, [&workspace] {
        if (auto* doc = Active(workspace)) workspace.SaveDocument(*doc);
    }, {});
    ui.SetSaveAllAction([&workspace] { (void)SaveAll(workspace); });
    engine.OnExitRequested = [&workspace](Engine::ExitSource) {
        if (auto* doc = Active(workspace)) doc->CommitEdit();
        return std::any_of(workspace.Documents.begin(), workspace.Documents.end(),
            [](const auto& document) { return document->IsDirty(); })
            ? Engine::ExitDecision::Defer : Engine::ExitDecision::Allow;
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
            if (!workspace.DocumentError.empty()) ImGui::TextWrapped("%s", workspace.DocumentError.c_str());
            if (ImGui::Button("Save all and close") && SaveAll(workspace))
            {
                ImGui::CloseCurrentPopup();
                engine.ConfirmExit();
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
