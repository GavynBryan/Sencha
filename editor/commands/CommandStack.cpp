#include "CommandStack.h"

void CommandStack::Execute(std::unique_ptr<ICommand> command)
{
    if (command == nullptr)
        return;

    if (Cursor < Commands.size())
        Commands.erase(Commands.begin() + static_cast<std::ptrdiff_t>(Cursor), Commands.end());

    command->Execute();
    Commands.push_back(std::move(command));
    Cursor = Commands.size();
}

void CommandStack::Undo()
{
    if (!CanUndo())
        return;

    --Cursor;
    Commands[Cursor]->Undo();
}

void CommandStack::Redo()
{
    if (!CanRedo())
        return;

    Commands[Cursor]->Execute();
    ++Cursor;
}

bool CommandStack::CanUndo() const
{
    return Cursor > 0;
}

bool CommandStack::CanRedo() const
{
    return Cursor < Commands.size();
}
