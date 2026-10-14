#include "LevelOperations.h"

#include "LevelDrafts.h"
#include "LevelMutations.h"
#include "LevelQueries.h"

#include <iterator>

namespace
{
    void Append(std::vector<EditorOperationEntry>& entries, std::vector<EditorOperationEntry> more)
    {
        std::move(more.begin(), more.end(), std::back_inserter(entries));
    }
}

std::vector<EditorOperationEntry> LevelOperations(LevelTarget& level, EditorOperations& operations)
{
    std::vector<EditorOperationEntry> entries = LevelQueryOperations(level);
    Append(entries, LevelMutationOperations(level));
    Append(entries, LevelDraftOperations(level, operations));
    return entries;
}
