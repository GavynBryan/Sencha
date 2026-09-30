#pragma once

#include "documents/DocumentSource.h"

class CommandStack;
class DocumentSourceSet;
class EditorWorkspace;

// The open level or world as one document in the application's journal. A
// command may span zones, so the world steps as one document; its key names
// the level editor rather than a file, so it survives Save As.
class LevelDocumentSource final : public DocumentSource
{
public:
    static constexpr std::string_view kKey = "level";

    LevelDocumentSource(EditorWorkspace& workspace, CommandStack& commands, DocumentSourceSet& sources);
    ~LevelDocumentSource() override;

    LevelDocumentSource(const LevelDocumentSource&) = delete;
    LevelDocumentSource& operator=(const LevelDocumentSource&) = delete;

    [[nodiscard]] DocumentRef Ref() { return { this, std::string(kKey) }; }

    void AppendChangedDocuments(std::vector<DocumentRef>& out) override;
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view key) override;
    [[nodiscard]] bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) override;
    void StepDocument(std::string_view key, DocumentStep step) override;
    void CancelDocumentEdits() override;
    void DiscardDocument(std::string_view key) override;
    [[nodiscard]] std::string DocumentLabel(std::string_view key) const override;

private:
    [[nodiscard]] bool WriteFiles();

    EditorWorkspace& Workspace;
    CommandStack& Commands;
    DocumentSourceSet& Sources;
};
