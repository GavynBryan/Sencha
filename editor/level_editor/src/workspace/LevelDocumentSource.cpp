#include "workspace/LevelDocumentSource.h"

#include "commands/CommandStack.h"
#include "documents/DocumentSourceSet.h"
#include "workspace/EditorWorkspace.h"

#include <string>

LevelDocumentSource::LevelDocumentSource(EditorWorkspace& workspace, CommandStack& commands,
                                         DocumentSourceSet& sources)
    : Workspace(workspace)
    , Commands(commands)
    , Sources(sources)
{
    Sources.AddSource(*this);
    Commands.SetExecuteObserver([this] { Sources.Record(Ref()); });
    // The stack is cleared when the edited document is replaced or refocused,
    // and the steps it dropped are no longer the journal's to retake.
    Commands.SetClearObserver([this] { Sources.ForgetDocument(Ref()); });
}

LevelDocumentSource::~LevelDocumentSource()
{
    Commands.SetExecuteObserver({});
    Commands.SetClearObserver({});
    Sources.RemoveSource(*this);
}

void LevelDocumentSource::AppendChangedDocuments(std::vector<DocumentRef>& out)
{
    if (Workspace.World.IsDirty() || Commands.HasPendingEdit())
        out.push_back(Ref());
}

bool LevelDocumentSource::WriteFiles()
{
    // A live preview is what the author is looking at, so it is committed as
    // an undo step rather than left out of what is saved.
    Workspace.ResolvePendingEdits();
    return Workspace.World.Save();
}

DocumentSaveResult LevelDocumentSource::SaveDocument(std::string_view)
{
    WorldDocument& world = Workspace.World;
    if (!world.HasSaveTarget())
        return { {}, DocumentSaveStatus::Failed, "It has never been saved; choose where with Save As." };
    if (world.IsExternallyModified())
        return { {}, DocumentSaveStatus::Conflict, {} };
    if (!WriteFiles())
        return { {}, DocumentSaveStatus::Failed, "Its files could not be written; the log says which." };
    return { {}, DocumentSaveStatus::Saved, {} };
}

bool LevelDocumentSource::SettleDocument(std::string_view, ConflictChoice choice, std::string& error)
{
    WorldDocument& world = Workspace.World;
    if (choice == ConflictChoice::KeepMine)
    {
        if (WriteFiles())
            return true;
        error = "The level's files could not be written; the log says which.";
        return false;
    }
    // Taking the file's version is reopening it; the reload replaces the
    // documents, which clears the history they had.
    const std::string path = world.IsWorld() ? std::string(world.WorldPath())
                                             : std::string(world.FocusDocument().GetDisplayName());
    const bool loaded = world.IsWorld() ? world.LoadWorld(path) : world.Load(path);
    if (!loaded)
        error = "'" + path + "' could not be read back.";
    return loaded;
}

void LevelDocumentSource::StepDocument(std::string_view, DocumentStep step)
{
    step == DocumentStep::Undo ? Commands.Undo() : Commands.Redo();
}

void LevelDocumentSource::CancelDocumentEdits()
{
    Workspace.CancelOpenEdits();
}

void LevelDocumentSource::DiscardDocument(std::string_view)
{
    Workspace.CancelDocumentTransactions();
    Workspace.World.DiscardChanges();
}

std::string LevelDocumentSource::DocumentLabel(std::string_view) const
{
    const WorldDocument& world = Workspace.World;
    return world.IsWorld() ? std::string(world.WorldPath()) : std::string(world.FocusDocument().GetDisplayName());
}
