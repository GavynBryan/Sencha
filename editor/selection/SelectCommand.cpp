#include "SelectCommand.h"

#include "SelectionService.h"

SelectCommand::SelectCommand(SelectionService& service, SelectableRef selection)
    : Service(service)
    , NextSelection(selection)
{
}

void SelectCommand::Execute()
{
    if (!CapturedPreviousSelection)
    {
        PreviousSelection = Service.GetPrimarySelection();
        CapturedPreviousSelection = true;
    }

    Service.ApplySelection(NextSelection);
}

void SelectCommand::Undo()
{
    Service.ApplySelection(PreviousSelection);
}
