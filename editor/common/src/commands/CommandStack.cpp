#include "CommandStack.h"

#include <utility>

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
    if (PendingEditCancel)
    {
        // Cleared before invoking: the callback runs the owning tool's cancel
        // transition, which calls ClosePendingEdit again (a no-op by then).
        std::function<void()> cancel = std::move(PendingEditCancel);
        PendingEditCancel = nullptr;
        cancel();
        return;
    }

    if (Cursor == 0)
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

void CommandStack::Clear()
{
    PendingEditCancel = nullptr;
    Commands.clear();
    Cursor = 0;
}

bool CommandStack::CanUndo() const
{
    return HasPendingEdit() || Cursor > 0;
}

bool CommandStack::CanRedo() const
{
    return Cursor < Commands.size();
}

void CommandStack::OpenPendingEdit(std::function<void()> cancel)
{
    if (PendingEditCancel)
    {
        // A second owner claiming the scope cancels the incumbent: its callback
        // reverts the state it staged and re-closes the scope on the way out.
        // Cleared before invoking, as in Undo, so that re-entrant close cannot
        // reach the callback being installed here.
        std::function<void()> incumbent = std::move(PendingEditCancel);
        PendingEditCancel = nullptr;
        incumbent();
    }

    PendingEditCancel = std::move(cancel);
}

void CommandStack::ClosePendingEdit()
{
    PendingEditCancel = nullptr;
}

bool CommandStack::HasPendingEdit() const
{
    return static_cast<bool>(PendingEditCancel);
}
