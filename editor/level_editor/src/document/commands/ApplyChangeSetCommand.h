#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"
#include "document/EntityChangeSet.h"

#include <utility>

// One undo step for a change set. Constructed holding the side the document
// is on: Before for a change set still to apply (an accepted draft), After for
// one whose edits were already made (an operation run on the live document),
// whose first Execute only announces that handles changed.
class ApplyChangeSetCommand final : public ICommand
{
public:
    ApplyChangeSetCommand(EditorDocument& document, EntityChangeSet changes, ChangeSetSide holding)
        : Document(document)
        , Changes(std::move(changes))
        , Holding(holding)
    {
    }

    void Execute() override
    {
        if (Holding == ChangeSetSide::After)
        {
            Document.AnnounceHandlesChanged();
            return;
        }
        ApplyEntityChangeSet(Document, Changes, ChangeSetSide::After);
        Holding = ChangeSetSide::After;
    }

    void Undo() override
    {
        ApplyEntityChangeSet(Document, Changes, ChangeSetSide::Before);
        Holding = ChangeSetSide::Before;
    }

    [[nodiscard]] const EntityChangeSet& ChangeSet() const { return Changes; }

private:
    EditorDocument& Document;
    EntityChangeSet Changes;
    ChangeSetSide Holding;
};
