#include "commands/CommandStack.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
// Counts Execute/Undo calls against externally owned tallies so assertions
// survive the stack owning (and possibly dropping) the command.
class CountingCommand : public ICommand
{
public:
    CountingCommand(int& executes, int& undos) : Executes(executes), Undos(undos) {}
    void Execute() override { ++Executes; }
    void Undo() override { ++Undos; }

private:
    int& Executes;
    int& Undos;
};
}

TEST(CommandStack, UndoCancelsOpenPendingEditFirst)
{
    CommandStack stack;
    int executes = 0;
    int undos = 0;
    stack.Execute(std::make_unique<CountingCommand>(executes, undos));

    int cancels = 0;
    stack.OpenPendingEdit([&] { ++cancels; });

    stack.Undo();
    EXPECT_EQ(cancels, 1);
    EXPECT_EQ(undos, 0); // the committed entry is untouched
    EXPECT_FALSE(stack.HasPendingEdit());
}

TEST(CommandStack, SecondUndoHitsCommittedEntry)
{
    CommandStack stack;
    int executes = 0;
    int undos = 0;
    stack.Execute(std::make_unique<CountingCommand>(executes, undos));
    stack.OpenPendingEdit([] {});

    stack.Undo();
    stack.Undo();
    EXPECT_EQ(undos, 1);
    EXPECT_FALSE(stack.CanUndo());
}

TEST(CommandStack, ClosePendingEditIsIdempotentAndNeverRunsTheCallback)
{
    CommandStack stack;
    int cancels = 0;
    stack.OpenPendingEdit([&] { ++cancels; });

    stack.ClosePendingEdit();
    stack.ClosePendingEdit();
    EXPECT_EQ(cancels, 0);
    EXPECT_FALSE(stack.HasPendingEdit());

    stack.Undo(); // nothing pending, nothing committed: a no-op
    EXPECT_EQ(cancels, 0);
}

TEST(CommandStack, ExecuteWhileScopeOpenKeepsScopeOpen)
{
    CommandStack stack;
    int executes = 0;
    int undos = 0;
    int cancels = 0;
    stack.OpenPendingEdit([&] { ++cancels; });
    stack.Execute(std::make_unique<CountingCommand>(executes, undos));

    EXPECT_TRUE(stack.HasPendingEdit());
    // Undo rolls the open pending edit back before the later command.
    stack.Undo();
    EXPECT_EQ(cancels, 1);
    EXPECT_EQ(undos, 0);
    stack.Undo();
    EXPECT_EQ(undos, 1);
}

TEST(CommandStack, CanUndoTrueWithOnlyPendingEdit)
{
    CommandStack stack;
    EXPECT_FALSE(stack.CanUndo());
    stack.OpenPendingEdit([] {});
    EXPECT_TRUE(stack.CanUndo());
    stack.ClosePendingEdit();
    EXPECT_FALSE(stack.CanUndo());
}

TEST(CommandStack, RedoIgnoresPendingEditScope)
{
    CommandStack stack;
    int executes = 0;
    int undos = 0;
    stack.Execute(std::make_unique<CountingCommand>(executes, undos));
    stack.Undo();
    EXPECT_TRUE(stack.CanRedo());

    stack.OpenPendingEdit([] {});
    EXPECT_TRUE(stack.CanRedo());
    stack.Redo();
    EXPECT_EQ(executes, 2);
    EXPECT_TRUE(stack.HasPendingEdit()); // a canceled pending edit was never a command
}

TEST(CommandStack, CancelCallbackMayCloseReentrantly)
{
    CommandStack stack;
    int cancels = 0;
    // The owning tool's cancel transition closes the scope itself; by then the
    // stack has already cleared it, so the nested close must be a silent no-op.
    stack.OpenPendingEdit(
        [&]
        {
            ++cancels;
            stack.ClosePendingEdit();
            EXPECT_FALSE(stack.HasPendingEdit());
        });

    stack.Undo();
    EXPECT_EQ(cancels, 1);
    EXPECT_FALSE(stack.HasPendingEdit());
}

TEST(CommandStack, ClearDropsScopeWithoutCancel)
{
    CommandStack stack;
    int cancels = 0;
    stack.OpenPendingEdit([&] { ++cancels; });

    stack.Clear();
    EXPECT_EQ(cancels, 0);
    EXPECT_FALSE(stack.HasPendingEdit());
    EXPECT_FALSE(stack.CanUndo());
}
