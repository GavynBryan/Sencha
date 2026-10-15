#include "LevelOperations.h"

#include "AssetQueries.h"
#include "BrushOperations.h"
#include "ComponentOperations.h"
#include "InstanceOperations.h"
#include "LevelChanges.h"
#include "LevelDrafts.h"
#include "LevelMutations.h"
#include "LevelQueries.h"
#include "MaterialOperations.h"
#include "NoteOperations.h"
#include "SpaceOperations.h"
#include "WorldCrossings.h"

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
    Append(entries, SpaceOperations(level));
    Append(entries, BrushOperations(level));
    Append(entries, AssetQueries(level));
    Append(entries, MaterialOperations(level));
    Append(entries, ComponentOperations(level));
    Append(entries, InstanceOperations(level));
    Append(entries, WorldCrossingOperations(level));
    Append(entries, NoteOperations(level));
    Append(entries, LevelDraftOperations(level, operations));
    Append(entries, LevelChangeOperations(level));
    return entries;
}
