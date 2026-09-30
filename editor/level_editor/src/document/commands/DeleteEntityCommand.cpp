#include "DeleteEntityCommand.h"

#include "commands/CompositeCommand.h"
#include "selection/SelectableRef.h"
#include "selection/commands/SelectCommand.h"

#include <algorithm>
#include <utility>
#include <vector>

DeleteEntityCommand::DeleteEntityCommand(EntityId entity,
                                         EditorScene& scene,
                                         EditorDocument& document)
    : Current(entity)
    , Scene(scene)
    , Document(document)
{
}

void DeleteEntityCommand::Execute()
{
    // Capture once: the snapshot is id-independent, so it stays valid across
    // undo/redo even though the entity is recreated under a fresh id.
    if (!Captured)
    {
        Snapshot = Document.CaptureEntity(Current);
        Captured = true;
    }

    Scene.DestroyEntity(Current);
    Document.MarkDirty();
}

void DeleteEntityCommand::Undo()
{
    Current = Document.RestoreEntity(Snapshot);
    Document.MarkDirty();
}

std::unique_ptr<ICommand> MakeDeleteEntitiesCommand(std::span<const EntityId> entities,
                                                    EditorScene& scene,
                                                    EditorDocument& document,
                                                    SelectionService& selection)
{
    // Deleting an entity means its branch: the tree the user sees promises
    // that. Expand each root to its subtree, deduplicating so a selection that
    // already contains both parent and child deletes each once.
    std::vector<EntityId> doomed;
    for (EntityId entity : entities)
    {
        if (std::find(doomed.begin(), doomed.end(), entity) != doomed.end())
            continue;
        std::vector<EntityId> subtree;
        scene.CollectSubtree(entity, subtree);
        for (EntityId member : subtree)
            if (std::find(doomed.begin(), doomed.end(), member) == doomed.end())
                doomed.push_back(member);
    }

    std::vector<std::unique_ptr<ICommand>> commands;
    commands.reserve(doomed.size() + 1);
    // Clear selection first so a deleted entity's handle is not left selected;
    // undo restores the prior selection (then the deletes are undone in reverse).
    commands.push_back(std::make_unique<SelectCommand>(selection, SelectableRef{}));
    // Leaf-up, so no child ever outlives its parent mid-composite -- and so the
    // reversed undo restores each parent before the children that resolve their
    // snapshot ParentId against it.
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
        commands.push_back(std::make_unique<DeleteEntityCommand>(*it, scene, document));
    return std::make_unique<CompositeCommand>(std::move(commands));
}
