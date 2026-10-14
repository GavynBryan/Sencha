#include "LevelEditTarget.h"

#include "commands/CommandStack.h"
#include "document/WorldDocument.h"
#include "document/commands/ApplyChangeSetCommand.h"

#include <memory>

EditorDocument& LiveLevelEdits::Document()
{
    return World.FocusDocument();
}

void LiveLevelEdits::Commit(EntityChangeSet changes)
{
    if (changes.Empty())
        return;
    Commands.Execute(std::make_unique<ApplyChangeSetCommand>(World.FocusDocument(), std::move(changes),
                                                             ChangeSetSide::After));
}
