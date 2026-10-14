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
        bool inserted = false;
        for (EntityId entity : PreviousOrder)
        {
            if (isMoved(entity))
                continue;
            if (entity == InsertBefore)
            {
                next.insert(next.end(), moved.begin(), moved.end());
                inserted = true;
            }
            next.push_back(entity);
        }
        // No anchor (or an anchor inside the moved block): the block lands last.
        if (!inserted)
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

ReparentTargets ResolveReparentTargets(std::span<const EntityId> entities, EntityId newParent,
                                       const EditorScene& scene, const EditorDocument& document, bool reorder)
{
    ReparentTargets targets;
    for (EntityId entity : entities)
    {
        if (!scene.HasEntity(entity))
            continue;
        // A placement member's position in its structure comes from the
        // source; there is no override a reparent could land in. Break the
        // instance to restructure. The root is the placement itself and moves freely.
        if (document.IsSceneInstanceMember(entity))
            return { {}, ReparentRefusal::PlacementMember };
        if (newParent.IsValid() && (newParent == entity || scene.IsAncestorOf(entity, newParent)))
            return { {}, ReparentRefusal::Cycle };
        // Already in place is a no-op -- unless the reparent is also an order,
        // where staying under the same parent is the point.
        if (!reorder && scene.GetParent(entity) == newParent)
            continue;
        // Reparenting both an entity and its ancestor would flatten the pair
        // instead of moving the branch.
        const bool ridesAlong = std::ranges::any_of(entities, [&](EntityId other) {
            return other != entity && scene.IsAncestorOf(other, entity);
        });
        if (!ridesAlong)
            targets.Entities.push_back(entity);
    }
    return targets;
}

std::unique_ptr<ICommand> MakeReparentEntitiesCommand(std::span<const EntityId> entities,
                                                      EntityId newParent,
                                                      ReparentTransformRule rule,
                                                      EditorScene& scene,
                                                      EditorDocument& document,
                                                      bool reorder, EntityId insertBefore)
{
    ReparentTargets targets = ResolveReparentTargets(entities, newParent, scene, document, reorder);
    if (targets.Refusal != ReparentRefusal::None || targets.Entities.empty())
        return nullptr;
    return std::make_unique<ReparentEntitiesCommand>(targets.Entities, newParent, rule, scene,
                                                     document, reorder, insertBefore);
}
