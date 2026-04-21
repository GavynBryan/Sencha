#pragma once

#include "../commands/ICommand.h"
#include "ISelectionContext.h"

class SelectCommand : public ICommand
{
public:
    SelectCommand(ISelectionContext& context, SelectableRef selection);

    void Execute() override;
    void Undo() override;

private:
    ISelectionContext& Context;
    SelectableRef PreviousSelection = {};
    SelectableRef NextSelection = {};
    bool CapturedPreviousSelection = false;
};
