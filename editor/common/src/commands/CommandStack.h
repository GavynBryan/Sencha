#pragma once

#include "ICommand.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

class CommandStack
{
public:
    void Execute(std::unique_ptr<ICommand> command);

    // With a pending edit open, Undo cancels it (runs the cancel callback) and
    // returns without touching committed entries; the next Undo reaches them.
    void Undo();
    void Redo();

    // Drops everything, including an open pending edit's callback WITHOUT
    // running it: Clear is document teardown, and the transient scene state a
    // pending edit guards is being destroyed wholesale with it. Callers keeping
    // the scene alive must cancel through the owning tool first.
    void Clear();

    [[nodiscard]] bool CanUndo() const;
    [[nodiscard]] bool CanRedo() const;

    // The single transient pending-edit scope: work that lives in the scene but
    // not on the stack yet (an uncommitted brush, a face-carve preview). The
    // owning mechanism opens it when that state comes alive, passing the
    // callback that reverts it, and closes it on its own commit or cancel
    // transition.
    //
    // At most one is open. Several mechanisms can stage previews (the brush and
    // face-carve tools, the cross-brush bridge, the inset/bevel edit), and any
    // of them may begin while another is pending, so opening over an open scope
    // runs the incumbent's cancel first and hands the scope to the new owner.
    // Cancel callbacks must therefore be idempotent through their owner's own
    // state, since they can be invoked by Undo, by their owner, or by the next
    // owner opening.
    //
    // Close is idempotent and never runs the callback (the owner already
    // committed or reverted through its own transition). Executing commands
    // while a scope is open is allowed and leaves it open; Undo then rolls the
    // pending edit back before any of those commands.
    void OpenPendingEdit(std::function<void()> cancel);
    void ClosePendingEdit();
    [[nodiscard]] bool HasPendingEdit() const;

private:
    std::vector<std::unique_ptr<ICommand>> Commands;
    std::size_t Cursor = 0;
    std::function<void()> PendingEditCancel; // empty == no scope open
};
