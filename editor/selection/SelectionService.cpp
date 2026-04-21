#include "SelectionService.h"

SelectionService::SelectionService(ISelectionContext& context)
    : Context(context)
{
}

SelectableRef SelectionService::GetPrimarySelection() const
{
    return Context.GetPrimarySelection();
}

void SelectionService::ApplySelection(SelectableRef selection)
{
    Context.SetPrimarySelection(selection);
}

void SelectionService::ClearSelection()
{
    Context.SetPrimarySelection({});
}

ISelectionContext& SelectionService::GetContext()
{
    return Context;
}

const ISelectionContext& SelectionService::GetContext() const
{
    return Context;
}
