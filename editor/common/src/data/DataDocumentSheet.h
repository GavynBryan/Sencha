#pragma once

#include "data/DataDocumentSet.h"
#include "data/DataProperties.h"
#include "data/DataSubtypeAuthoring.h"
#include "problems/ProblemList.h"
#include "prompts/PromptQueue.h"
#include "properties/FilePathProperty.h"
#include "workspaces/WorkspaceFacets.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

union SDL_Event;
class ProjectFileSteps;

// A workspace's open data documents presented as sheets: the active one laid
// out by its schema with what its subtype adds, every one on the document dial,
// each closed after asking (editor/ARCHITECTURE.md, Data workspace).
class DataDocumentSheet
{
public:
    // Without `files`, a document stays where it is.
    DataDocumentSheet(DataDocumentSet& documents, PromptQueue& prompts, const DataSubtypeAuthoringSetup& authorings,
                      ProjectFileSteps* files);

    DataDocumentSheet(const DataDocumentSheet&) = delete;
    DataDocumentSheet& operator=(const DataDocumentSheet&) = delete;

    [[nodiscard]] DataDocumentSet& Documents() { return Set; }
    [[nodiscard]] DataProperties& Fields() { return Properties; }

    // Per frame while it is seen: the active document's subtype reads what it
    // is read against, and makes the write it is waiting to make.
    void Update();
    [[nodiscard]] bool ClaimPlatformEvent(const SDL_Event& event);
    // The active document's card: its file, its fields (or `fields` in their
    // place), and its subtype's previews; none without an active document.
    [[nodiscard]] std::vector<PropertySection> Sections(IPropertySource* fields = nullptr);

    void AppendOpen(WorkspaceDocumentFacet& facet) const;
    [[nodiscard]] std::optional<WorkspaceOpenDocument> ActiveEntry() const;
    [[nodiscard]] std::optional<DocumentRef> ActiveRef();
    [[nodiscard]] bool Owns(const DocumentRef& document) const;
    // False when `key` names none of its documents.
    bool Show(std::string_view key);
    bool Open(std::string_view path);
    // Asks before the active document's changes go, then closes it; `closed`
    // hears its path once it has.
    void CloseActive(std::function<void(const std::string& path)> closed = {});

    // Every open document's validation errors, and what the last operation could not do.
    [[nodiscard]] std::vector<Problem> Problems() const;
    void SetError(std::string error) { OperationError = std::move(error); }

private:
    [[nodiscard]] IDataSubtypeAuthoring* ActiveAuthoring(const DataDocument*& document);

    DataDocumentSet& Set;
    PromptQueue::Asker Prompts;
    DataSubtypeAuthoringRegistry Authoring;
    DataProperties Properties;
    FilePathProperty FilePath;
    ProjectFileSteps* Files = nullptr;
    std::string OperationError;
};
