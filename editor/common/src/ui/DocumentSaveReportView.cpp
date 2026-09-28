#include "ui/DocumentSaveReportView.h"

#include "documents/DocumentSourceSet.h"

#include <imgui.h>

#include <vector>

namespace
{
    constexpr ImVec4 kErrorColor(1.0f, 0.5f, 0.4f, 1.0f);
}

std::string DescribeDocumentSave(const DocumentSaveResult& result)
{
    switch (result.Status)
    {
    case DocumentSaveStatus::Saved:
    case DocumentSaveStatus::SavedWithProblems:
        return {};
    case DocumentSaveStatus::Conflict:
        return "The file changed on disk since it was read. Keep yours or take the file's under Changes.";
    case DocumentSaveStatus::Failed:
        return result.Error;
    }
    return {};
}

void DrawUnsavedDocuments(const DocumentSourceSet& sources)
{
    const std::vector<DocumentRef> changed = sources.ChangedDocuments();
    for (const DocumentRef& document : changed)
        ImGui::BulletText("%s: unsaved edits", document.Key.c_str());
    if (changed.empty())
        ImGui::TextDisabled("No open document has unsaved edits.");
}

void DrawDocumentSaveReport(DocumentSourceSet& sources, std::string& settleError)
{
    const DocumentSaveReport& report = sources.LastSave();
    for (const DocumentSaveResult* result : report.WithStatus(DocumentSaveStatus::SavedWithProblems))
        ImGui::TextWrapped("%s was saved with problems a game will refuse to load.", result->Document.Key.c_str());
    for (const DocumentSaveResult* result : report.WithStatus(DocumentSaveStatus::Failed))
        ImGui::TextColored(kErrorColor, "%s was not saved: %s", result->Document.Key.c_str(), result->Error.c_str());

    // Settling one removes it from the report being walked.
    std::vector<DocumentRef> conflicts;
    for (const DocumentSaveResult* result : report.WithStatus(DocumentSaveStatus::Conflict))
        conflicts.push_back(result->Document);
    for (const DocumentRef& document : conflicts)
    {
        ImGui::PushID(document.Key.c_str());
        ImGui::TextWrapped("%s changed on disk since it was read.", document.Key.c_str());
        if (ImGui::SmallButton("Keep mine"))
            (void)sources.Settle(document, ConflictChoice::KeepMine, settleError);
        ImGui::SameLine();
        if (ImGui::SmallButton("Take the file's"))
            (void)sources.Settle(document, ConflictChoice::TakeFile, settleError);
        ImGui::PopID();
    }
    if (!settleError.empty())
        ImGui::TextColored(kErrorColor, "%s", settleError.c_str());
}
