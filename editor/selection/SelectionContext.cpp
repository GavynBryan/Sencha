#include "SelectionContext.h"

SelectableRef SelectionContext::GetPrimarySelection() const
{
    return PrimarySelection;
}

void SelectionContext::SetPrimarySelection(SelectableRef selection)
{
    PrimarySelection = selection;
}
