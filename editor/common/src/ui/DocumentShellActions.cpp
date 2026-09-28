#include "ui/DocumentShellActions.h"

#include "documents/DocumentSourceSet.h"
#include "ui/DocumentSaveReportView.h"
#include "ui/EditorUiFeature.h"

#include <imgui.h>

#include <memory>

Engine::ExitDecision DecideDocumentExit(const DocumentSourceSet& sources)
{
    return sources.ChangedDocuments().empty() ? Engine::ExitDecision::Allow : Engine::ExitDecision::Defer;
}

void InstallDocumentShellActions(EditorUiFeature& ui, Engine& engine, DocumentSourceSet& sources,
                                 std::function<std::optional<DocumentRef>()> activeDocument)
{
    ui.SetUndoActions([&sources] { sources.Undo(); }, [&sources] { sources.Redo(); },
                      [&sources] { return sources.CanUndo(); }, [&sources] { return sources.CanRedo(); });
    ui.SetFileActions({}, {}, [&sources, activeDocument = std::move(activeDocument)] {
        if (const std::optional<DocumentRef> document = activeDocument())
            (void)sources.Save(*document);
    }, {});
    ui.SetSaveAllAction([&sources] { (void)sources.SaveAll(); });
    engine.OnExitRequested = [&sources](Engine::ExitSource) { return DecideDocumentExit(sources); };

    auto settleError = std::make_shared<std::string>();
    ui.AddOverlay([&engine, &sources, settleError] {
        if (!engine.IsExitPending())
            return;
        constexpr const char* title = "Unsaved documents";
        if (!ImGui::IsPopupOpen(title))
            ImGui::OpenPopup(title);
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextUnformatted("Save changes before closing?");
        DrawUnsavedDocuments(sources);
        DrawDocumentSaveReport(sources, *settleError);
        if (ImGui::Button("Save all and close"))
        {
            (void)sources.SaveAll();
            if (DecideDocumentExit(sources) == Engine::ExitDecision::Allow)
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
    });
}
