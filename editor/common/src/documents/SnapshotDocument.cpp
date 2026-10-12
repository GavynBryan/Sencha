#include "documents/SnapshotDocument.h"

#include <core/io/FileBytes.h>

#include <format>
#include <fstream>
#include <utility>

struct SnapshotStep final : ICommand
{
    SnapshotDocument& Document;
    std::string Before;
    std::string After;
    // The holder already has `After` when the step is taken.
    bool Taken = true;

    SnapshotStep(SnapshotDocument& document, std::string before, std::string after)
        : Document(document)
        , Before(std::move(before))
        , After(std::move(after))
    {
    }

    void Execute() override
    {
        if (Taken)
            Taken = false;
        else
            Document.Apply(After);
    }
    void Undo() override { Document.Apply(Before); }
};

SnapshotDocument::SnapshotDocument(DocumentSourceSet& sources, SnapshotValue value)
    : Sources(sources)
    , Value(std::move(value))
{
    History.SetExecuteObserver([this] { Sources.Record(Ref()); });
    Sources.AddSource(*this);
}

SnapshotDocument::~SnapshotDocument()
{
    Sources.RemoveSource(*this);
}

void SnapshotDocument::Open(std::filesystem::path file)
{
    Close();
    std::string text;
    std::string error;
    if (!Value.Write(text, error))
        text.clear();
    File = std::move(file);
    Recorded = text;
    Saved = std::move(text);
    Baseline.Record(File);
}

void SnapshotDocument::Close()
{
    if (!IsOpen())
        return;
    Editing = false;
    History.Clear();
    Sources.ForgetDocument(Ref());
    File.clear();
    Recorded.clear();
    Saved.clear();
}

void SnapshotDocument::Changed()
{
    if (!IsOpen() || Editing)
        return;
    std::string now;
    std::string error;
    if (!Value.Write(now, error) || now == Recorded)
        return;
    std::string before = std::exchange(Recorded, now);
    History.Execute(std::make_unique<SnapshotStep>(*this, std::move(before), std::move(now)));
}

void SnapshotDocument::BeginEdit()
{
    if (!IsOpen() || Editing)
        return;
    Changed();
    Editing = true;
    // Undo, or a newer pending edit elsewhere, cancels this one first.
    History.OpenPendingEdit([this] { CancelEdit(); });
}

void SnapshotDocument::CommitEdit()
{
    if (!Editing)
        return;
    Editing = false;
    History.ClosePendingEdit();
    Changed();
}

void SnapshotDocument::CancelEdit()
{
    if (!Editing)
        return;
    Editing = false;
    History.ClosePendingEdit();
    Apply(Recorded);
}

bool SnapshotDocument::Revert(std::string& error)
{
    CancelEdit();
    std::string text;
    if (!IsOpen() || !ReadFile(text, error) || !Value.Read(text, error))
        return false;
    Saved = text;
    Baseline.Record(File);
    if (text != Recorded)
    {
        std::string before = std::exchange(Recorded, text);
        History.Execute(std::make_unique<SnapshotStep>(*this, std::move(before), std::move(text)));
    }
    return true;
}

void SnapshotDocument::Apply(const std::string& text)
{
    std::string error;
    (void)Value.Read(text, error);
    Recorded = text;
}

bool SnapshotDocument::ReadFile(std::string& text, std::string& error) const
{
    std::vector<std::byte> bytes;
    if (!ReadFileBytes(File, bytes))
    {
        error = std::format("{} could not be read.", File.generic_string());
        return false;
    }
    text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool SnapshotDocument::WriteFile(std::string& error)
{
    Changed();
    std::string text;
    if (!Value.Write(text, error))
        return false;
    std::ofstream out(File, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out.good())
    {
        error = std::format("{} could not be written.", File.generic_string());
        return false;
    }
    out.close();
    Recorded = text;
    Saved = std::move(text);
    Baseline.Record(File);
    return true;
}

void SnapshotDocument::AppendChangedDocuments(std::vector<DocumentRef>& out)
{
    if (IsOpen() && IsChanged())
        out.push_back(Ref());
}

DocumentSaveResult SnapshotDocument::SaveDocument(std::string_view key)
{
    if (!Holds(key))
        return { {}, DocumentSaveStatus::Failed, "That document is not open." };
    CommitEdit();
    if (Baseline.FileChanged(File))
        return { {}, DocumentSaveStatus::Conflict,
                 std::format("{} changed on disk since it was read.", File.generic_string()) };
    std::string error;
    if (!WriteFile(error))
        return { {}, DocumentSaveStatus::Failed, std::move(error) };
    return { {}, DocumentSaveStatus::Saved, {} };
}

bool SnapshotDocument::SettleDocument(std::string_view key, ConflictChoice choice, std::string& error)
{
    if (!Holds(key))
    {
        error = "That document is not open.";
        return false;
    }
    if (choice == ConflictChoice::KeepMine)
        return WriteFile(error);
    return Revert(error);
}

void SnapshotDocument::StepDocument(std::string_view key, DocumentStep step)
{
    if (!Holds(key))
        return;
    CancelEdit();
    step == DocumentStep::Undo ? History.Undo() : History.Redo();
}

void SnapshotDocument::DiscardDocument(std::string_view key)
{
    if (!Holds(key))
        return;
    CancelEdit();
    Apply(Saved);
    History.Clear();
    Sources.ForgetDocument(Ref());
}

std::string SnapshotDocument::DocumentLabel(std::string_view key) const
{
    return Holds(key) ? File.filename().string() : std::string(key);
}

bool SnapshotDocument::HoldsFile(const std::filesystem::path& file) const
{
    std::error_code ec;
    return IsOpen() && std::filesystem::equivalent(file, File, ec);
}
