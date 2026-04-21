#include "SelectCommand.h"

SelectCommand::SelectCommand(ISelectionContext& context, SelectableRef selection)
    : Context(context)
    , NextSelection(selection)
{
}

void SelectCommand::Execute()
{
    if (!CapturedPreviousSelection)
    {
        PreviousSelection = Context.GetPrimarySelection();
        CapturedPreviousSelection = true;
    }

    Context.SetPrimarySelection(NextSelection);
}

void SelectCommand::Undo()
{
    Context.SetPrimarySelection(PreviousSelection);
}
