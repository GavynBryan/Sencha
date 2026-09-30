#include "ui/DocumentSaveReportView.h"

#include "documents/DocumentSourceSet.h"

#include <imgui.h>

#include <vector>

namespace
{
    std::string LabelOf(const DocumentRef& document)
    {
        return document.Source != nullptr ? document.Source->DocumentLabel(document.Key) : document.Key;
    }
}

std::string DescribeDocumentSave(const DocumentSaveResult& result)
{
    switch (result.Status)
    {
    case DocumentSaveStatus::Saved:
    case DocumentSaveStatus::SavedWithProblems:
        return {};
    case DocumentSaveStatus::Conflict:
        return "The file changed on disk since it was read; the editor asks which version to keep.";
    case DocumentSaveStatus::Failed:
        return result.Error;
    }
    return {};
}

void DrawUnsavedDocuments(const DocumentSourceSet& sources)
{
    const std::vector<DocumentRef> changed = sources.ChangedDocuments();
    for (const DocumentRef& document : changed)
        ImGui::BulletText("%s: unsaved edits", LabelOf(document).c_str());
    if (changed.empty())
        ImGui::TextDisabled("No open document has unsaved edits.");
}
