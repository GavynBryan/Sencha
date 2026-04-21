#pragma once

#include "../commands/ICommand.h"
#include "SelectableRef.h"

class SelectionService;

class SelectCommand : public ICommand
{
public:
    SelectCommand(SelectionService& service, SelectableRef selection);

    void Execute() override;
    void Undo() override;

private:
    SelectionService& Service;
    SelectableRef PreviousSelection = {};
    SelectableRef NextSelection = {};
    bool CapturedPreviousSelection = false;
};
