#pragma once

#include "ICommand.h"

#include <cstddef>
#include <memory>
#include <vector>

class CommandStack
{
public:
    void Execute(std::unique_ptr<ICommand> command);
    void Undo();
    void Redo();

    [[nodiscard]] bool CanUndo() const;
    [[nodiscard]] bool CanRedo() const;

private:
    std::vector<std::unique_ptr<ICommand>> Commands;
    std::size_t Cursor = 0;
};
