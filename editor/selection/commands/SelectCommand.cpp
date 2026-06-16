#include "SelectCommand.h"

#include "../SelectionService.h"

#include <utility>

SelectCommand::SelectCommand(SelectionService& service, SelectableRef selection)
    : Service(service)
    , NextSelection(selection.IsValid()
        ? SelectionSnapshot{ .Items = { selection }, .Primary = selection }
        : SelectionSnapshot{})
{
}

SelectCommand::SelectCommand(SelectionService& service, SelectionSnapshot selection)
    : Service(service)
    , NextSelection(std::move(selection))
{
}

void SelectCommand::Execute()
{
    if (!CapturedPreviousSelection)
    {
        PreviousSelection = Service.GetSnapshot();
        CapturedPreviousSelection = true;
    }

    Service.ApplySnapshot(NextSelection);
}

void SelectCommand::Undo()
{
    Service.ApplySnapshot(PreviousSelection);
}
