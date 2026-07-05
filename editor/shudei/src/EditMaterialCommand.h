#pragma once

#include "MaterialEditSession.h"

#include "commands/ICommand.h"

#include <utility>

// One property edit as a value swap of the whole description: cheap (a few
// strings and floats), and undo/redo re-previews for free because the session
// version bumps on every SetWorking. The command stack is cleared when a
// different material is opened, so a command never outlives its target.
class EditMaterialCommand final : public ICommand
{
public:
    EditMaterialCommand(MaterialEditSession& session,
                        MaterialDescription before,
                        MaterialDescription after)
        : Session(session)
        , Before(std::move(before))
        , After(std::move(after))
    {
    }

    void Execute() override { Session.SetWorking(After); }
    void Undo() override { Session.SetWorking(Before); }

private:
    MaterialEditSession& Session;
    MaterialDescription Before;
    MaterialDescription After;
};
