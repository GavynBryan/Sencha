#pragma once

#include "document/EntityChangeSet.h"

class CommandStack;
class EditorDocument;
class WorldDocument;

// What a level mutation edits and where its change set goes: the live
// document, as one undo step, or a draft, as part of a proposal.
class LevelEditTarget
{
public:
    virtual ~LevelEditTarget() = default;

    [[nodiscard]] virtual EditorDocument& Document() = 0;
    // The edits in `changes` are already made to Document().
    virtual void Commit(EntityChangeSet changes) = 0;
};

// The world document's focus level, each change set one undo step on `commands`.
class LiveLevelEdits final : public LevelEditTarget
{
public:
    LiveLevelEdits(WorldDocument& world, CommandStack& commands)
        : World(world)
        , Commands(commands)
    {
    }

    [[nodiscard]] EditorDocument& Document() override;
    void Commit(EntityChangeSet changes) override;

private:
    WorldDocument& World;
    CommandStack& Commands;
};
