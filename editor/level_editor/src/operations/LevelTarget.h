#pragma once

#include "operations/EditorOperations.h"

#include <functional>

class CommandStack;
class EditorDocument;
class EntityChangeRecorder;
class WorldDocument;

// The level the level operations read and write: the world document's focus
// level, each mutation one undo step on `commands`.
class LevelTarget
{
public:
    // Edits the document, telling the recorder what it touches, and answers.
    using Edit = std::function<EditorOperationOutcome(EditorDocument&, EntityChangeRecorder&)>;

    LevelTarget(WorldDocument& world, CommandStack& commands)
        : World(world)
        , Commands(commands)
    {
    }

    [[nodiscard]] EditorDocument& Document();
    [[nodiscard]] const EditorDocument& Document() const;

    // Runs `edit` as one mutation: a refusal puts back whatever it had already
    // changed, and success adds what changed and the revisions to its answer.
    [[nodiscard]] EditorOperationOutcome Mutate(const Edit& edit);

private:
    WorldDocument& World;
    CommandStack& Commands;
};

// What every mutation's answer adds: created, modified and deleted entities and
// the revision before and after, followed by `extra`.
[[nodiscard]] DataFieldSchema MutationResultSchema(std::vector<DataFieldSchema> extra = {});
