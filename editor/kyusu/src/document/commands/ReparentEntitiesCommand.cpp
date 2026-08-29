#include "ReparentEntitiesCommand.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <algorithm>
#include <utility>

ReparentEntitiesCommand::ReparentEntitiesCommand(std::span<const EntityId> entities,
                                                 EntityId newParent,
                                                 ReparentTransformRule rule,
                                                 EditorScene& scene,
                                                 EditorDocument& document,
                                                 bool reorder, EntityId insertBefore)
    : Scene(scene)
    , Document(document)
    , NewParent(newParent)
    , Rule(rule)
    , Reorder(reorder)
    , InsertBefore(insertBefore)
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
        if (Reorder)
        {
            const std::span<const EntityId> order = Scene.GetAllEntities();
            PreviousOrder.assign(order.begin(), order.end());
        }
        Captured = true;
    }

    for (Entry& entry : Entries)
    {
        // A same-parent entity is here only for its order; touching its
        // transform would replace an authored local with a recomputation.
        if (entry.PreviousParent == NewParent)
            continue;
        if (!Scene.SetParent(entry.Entity, NewParent))
            continue;
        // The world value was captured against the old parentage; placing it
        // again under the new parent is exactly the no-visual-move contract.
        if (Rule == ReparentTransformRule::KeepWorld)
            Scene.SetWorldTransform(entry.Entity, entry.World);
    }

    if (Reorder)
    {
        const auto isMoved = [this](EntityId entity)
        {
            return std::any_of(Entries.begin(), Entries.end(),
                [&](const Entry& e) { return e.Entity == entity; });
        };
        // The moved block keeps its previous relative order; the rest of the
        // list keeps its order around the insertion point.
        std::vector<EntityId> moved;
        std::vector<EntityId> next;
        moved.reserve(Entries.size());
        next.reserve(PreviousOrder.size());
        for (EntityId entity : PreviousOrder)
            if (isMoved(entity))
                moved.push_back(entity);
        for (EntityId entity : PreviousOrder)
        {
            if (isMoved(entity))
                continue;
            if (entity == InsertBefore)
                next.insert(next.end(), moved.begin(), moved.end());
            next.push_back(entity);
        }
        if (next.size() != PreviousOrder.size())
            next.insert(next.end(), moved.begin(), moved.end());
        (void)Scene.SetEntityOrder(next);
    }
    Document.MarkDirty();
}

void ReparentEntitiesCommand::Undo()
{
    for (auto it = Entries.rbegin(); it != Entries.rend(); ++it)
    {
        if (it->PreviousParent == NewParent)
            continue;
        if (!Scene.SetParent(it->Entity, it->PreviousParent))
            continue;
        Scene.SetTransform(it->Entity, it->PreviousLocal);
    }
    if (Reorder)
        (void)Scene.SetEntityOrder(PreviousOrder);
    Document.MarkDirty();
}

std::unique_ptr<ICommand> MakeReparentEntitiesCommand(std::span<const EntityId> entities,
                                                      EntityId newParent,
                                                      ReparentTransformRule rule,
                                                      EditorScene& scene,
                                                      EditorDocument& document,
                                                      bool reorder, EntityId insertBefore)
{
    std::vector<EntityId> targets;
    targets.reserve(entities.size());
    for (EntityId entity : entities)
    {
        if (!scene.HasEntity(entity))
            continue;
        // A placement member's position in its structure comes from the
        // source; there is no override a reparent could land in (D4 excludes
        // it). Break the instance to restructure. The root is the placement
        // itself and moves freely.
        if (document.IsSceneInstanceMember(entity))
            return nullptr;
        // A cycle disqualifies the gesture: half a drop landing is worse than
        // the drop refusing, and the drop target already showed the refusal.
        if (newParent.IsValid()
            && (newParent == entity || scene.IsAncestorOf(entity, newParent)))
            return nullptr;
        // Already in place is a no-op -- unless the gesture is also an order,
        // where staying under the same parent is the point.
        if (!reorder && scene.GetParent(entity) == newParent)
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
    return std::make_unique<ReparentEntitiesCommand>(targets, newParent, rule, scene,
                                                     document, reorder, insertBefore);
}
