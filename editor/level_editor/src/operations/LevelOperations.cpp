#include "LevelOperations.h"

#include "LevelMutations.h"
#include "LevelQueries.h"

#include <iterator>

std::vector<EditorOperationEntry> LevelOperations(LevelTarget& level)
{
    std::vector<EditorOperationEntry> entries = LevelQueryOperations(level);
    std::vector<EditorOperationEntry> mutations = LevelMutationOperations(level);
    std::move(mutations.begin(), mutations.end(), std::back_inserter(entries));
    return entries;
}
