#pragma once

#include "commands/CommandStack.h"
#include "documents/DocumentSource.h"
#include "documents/DocumentSourceSet.h"
#include "documents/FileBaseline.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// How a value someone else holds is saved as text, and how a snapshot of that
// text is put back into it.
struct SnapshotValue
{
    // False, with why, when the value cannot be written.
    std::function<bool(std::string& text, std::string& error)> Write;
    // False, with why, when `text` is not a value the holder takes.
    std::function<bool(std::string_view text, std::string& error)> Read;
};

// A file whose whole value a session holds and edits in place (a sidecar),
// journaled as snapshots of the text it is saved as. The holder tells it of
// every change; an edit that previews is begun, then committed or cancelled.
class SnapshotDocument final : public DocumentSource
{
public:
    SnapshotDocument(DocumentSourceSet& sources, SnapshotValue value);
    ~SnapshotDocument() override;

    SnapshotDocument(const SnapshotDocument&) = delete;
    SnapshotDocument& operator=(const SnapshotDocument&) = delete;

    // Follows `file` from now, what the holder has taken as what it holds; the
    // previous file's changes were settled by whoever opens another.
    void Open(std::filesystem::path file);
    void Close();
    [[nodiscard]] bool IsOpen() const { return !File.empty(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return File; }
    [[nodiscard]] DocumentRef Ref() { return { this, File.generic_string() }; }
    [[nodiscard]] bool IsChanged() const { return Editing || Recorded != Saved; }
    // The file's version, taken as one step.
    [[nodiscard]] bool Revert(std::string& error);

    // A step from what was last recorded to what the holder has now, unless an
    // edit is open.
    void Changed();
    void BeginEdit();
    void CommitEdit();
    void CancelEdit();
    [[nodiscard]] bool IsEditing() const { return Editing; }

    void AppendChangedDocuments(std::vector<DocumentRef>& out) override;
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view key) override;
    [[nodiscard]] bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) override;
    void StepDocument(std::string_view key, DocumentStep step) override;
    void CancelDocumentEdits() override { CancelEdit(); }
    // Puts the saved version back; the document stays open, unchanged.
    void DiscardDocument(std::string_view key) override;
    [[nodiscard]] std::string DocumentLabel(std::string_view key) const override;
    [[nodiscard]] bool HoldsFile(const std::filesystem::path& file) const override;

private:
    friend struct SnapshotStep;

    [[nodiscard]] bool Holds(std::string_view key) const { return IsOpen() && key == File.generic_string(); }
    void Apply(const std::string& text);
    [[nodiscard]] bool WriteFile(std::string& error);
    [[nodiscard]] bool ReadFile(std::string& text, std::string& error) const;

    DocumentSourceSet& Sources;
    SnapshotValue Value;
    std::filesystem::path File;
    // The value as the journal last recorded it, and as the file last held it.
    std::string Recorded;
    std::string Saved;
    FileBaseline Baseline;
    CommandStack History;
    bool Editing = false;
};
