#pragma once

#include "commands/ICommand.h"
#include "selection/SelectableRef.h"
#include "selection/ISelectionContext.h"

class SelectionService;

class SelectCommand : public ICommand
{
public:
    SelectCommand(SelectionService& service, SelectableRef selection);
    SelectCommand(SelectionService& service, SelectionSnapshot selection);

    void Execute() override;
    void Undo() override;

private:
    SelectionService& Service;
    SelectionSnapshot PreviousSelection = {};
    SelectionSnapshot NextSelection = {};
    bool CapturedPreviousSelection = false;
};
