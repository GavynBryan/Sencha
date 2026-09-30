#include "ui/DocumentShellActions.h"

#include "documents/DocumentSourceSet.h"
#include "ui/DocumentSaveReportView.h"
#include "ui/EditorUiFeature.h"

#include <imgui.h>

#include <memory>
#include <optional>
#include <utility>

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
    ui.AddShellOverlay([&engine, &sources, settleError] {
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
            // Thrown away here, deliberately, so nothing is destroyed later
            // still holding changes.
            sources.DiscardAll();
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

void UnsavedDocumentPrompt::Ask(bool hasChanges, std::string documentName,
                                std::function<void(DirtyDisposition)> proceed)
{
    if (!hasChanges)
    {
        proceed(DirtyDisposition::Refuse);
        return;
    }
    DocumentName = std::move(documentName);
    Proceed = std::move(proceed);
    Asking = true;
}

void UnsavedDocumentPrompt::Draw()
{
    constexpr const char* title = "Unsaved changes";
    if (Asking && !ImGui::IsPopupOpen(title))
        ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::Text("%s has unsaved changes.", DocumentName.c_str());
    std::optional<DirtyDisposition> chosen;
    if (ImGui::Button("Save"))
        chosen = DirtyDisposition::Save;
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
        chosen = DirtyDisposition::Discard;
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (chosen || cancelled)
    {
        ImGui::CloseCurrentPopup();
        Asking = false;
        if (chosen)
            std::exchange(Proceed, {})(*chosen);
        Proceed = {};
    }
    ImGui::EndPopup();
}
