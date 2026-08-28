#include "ReparentEntitiesCommand.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <utility>

ReparentEntitiesCommand::ReparentEntitiesCommand(std::span<const EntityId> entities,
                                                 EntityId newParent,
                                                 ReparentTransformRule rule,
                                                 EditorScene& scene,
                                                 EditorDocument& document)
    : Scene(scene)
    , Document(document)
    , NewParent(newParent)
    , Rule(rule)
{
    Entries.reserve(entities.size());
    for (EntityId entity : entities)
        Entries.push_back(Entry{ .Entity = entity, .PreviousParent = EntityId{} });
}

void ReparentEntitiesCommand::Execute()
{
    // Capture once: redo replays the same previous-state record, so an
    // undo/redo cycle is exact rather than a fresh conversion of a conversion.
    if (!Captured)
    {
        for (Entry& entry : Entries)
        {
            entry.PreviousParent = Scene.GetParent(entry.Entity);
            if (const Transform3f* local = Scene.TryGetLocalTransform(entry.Entity))
                entry.PreviousLocal = *local;
            // Composed live, like the conversion that will consume it: the
            // derived component can be a refresh behind an edit made this frame.
            entry.World = Scene.ComposeWorldTransform(entry.Entity);
        }
        Captured = true;
    }

    for (Entry& entry : Entries)
    {
        if (!Scene.SetParent(entry.Entity, NewParent))
            continue;
        // The world value was captured against the old parentage; placing it
        // again under the new parent is exactly the no-visual-move contract.
        if (Rule == ReparentTransformRule::KeepWorld)
            Scene.SetWorldTransform(entry.Entity, entry.World);
    }
    Document.MarkDirty();
}

void ReparentEntitiesCommand::Undo()
{
    for (auto it = Entries.rbegin(); it != Entries.rend(); ++it)
    {
        if (!Scene.SetParent(it->Entity, it->PreviousParent))
            continue;
        Scene.SetTransform(it->Entity, it->PreviousLocal);
    }
    Document.MarkDirty();
}

std::unique_ptr<ICommand> MakeReparentEntitiesCommand(std::span<const EntityId> entities,
                                                      EntityId newParent,
                                                      ReparentTransformRule rule,
                                                      EditorScene& scene,
                                                      EditorDocument& document)
{
    std::vector<EntityId> targets;
    targets.reserve(entities.size());
    for (EntityId entity : entities)
    {
        if (!scene.HasEntity(entity))
            continue;
        // A cycle disqualifies the gesture: half a drop landing is worse than
        // the drop refusing, and the drop target already showed the refusal.
        if (newParent.IsValid()
            && (newParent == entity || scene.IsAncestorOf(entity, newParent)))
            return nullptr;
        if (scene.GetParent(entity) == newParent)
            continue;

        // An entity rides along with any ancestor also in the set; reparenting
        // both would flatten the pair instead of moving the branch.
        bool coveredByAncestor = false;
        for (EntityId other : entities)
            if (other != entity && scene.IsAncestorOf(other, entity))
            {
                coveredByAncestor = true;
                break;
            }
        if (!coveredByAncestor)
            targets.push_back(entity);
    }

    if (targets.empty())
        return nullptr;
    return std::make_unique<ReparentEntitiesCommand>(targets, newParent, rule, scene, document);
}
