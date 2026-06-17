#pragma once

#include "ICommand.h"

#include <memory>
#include <utility>
#include <vector>

// Groups several commands into one undoable step: execute forward, undo in
// reverse. Used e.g. when a single gizmo drag moves multiple selected entities —
// one drag should be one undo.
class CompositeCommand : public ICommand
{
public:
    explicit CompositeCommand(std::vector<std::unique_ptr<ICommand>> commands)
        : Commands(std::move(commands))
    {
    }

    void Execute() override
    {
        for (auto& command : Commands)
            command->Execute();
    }

    void Undo() override
    {
        for (auto it = Commands.rbegin(); it != Commands.rend(); ++it)
            (*it)->Undo();
    }

private:
    std::vector<std::unique_ptr<ICommand>> Commands;
};
