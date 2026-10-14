#include "EntityChangeSet.h"

#include "EditorDocument.h"
#include "EditorScene.h"

#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <ranges>

namespace
{
    PersistentEntityId IdOf(const EditorDocument& document, EntityId entity)
    {
        const auto* id = document.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
        return id != nullptr ? id->Id : PersistentEntityId{};
    }

    EntityId Resolve(const EditorDocument& document, PersistentEntityId id)
    {
        const auto* index = document.GetRegistry().Components.TryGetResource<PersistentEntityIndex>();
        return index != nullptr ? index->TryResolve(id) : EntityId{};
    }
}

EntityChangeRecorder::EntityChangeRecorder(EditorDocument& document)
    : Document(document)
{
}

void EntityChangeRecorder::Touch(EntityId entity)
{
    const PersistentEntityId id = IdOf(Document, entity);
    if (!id.IsValid() || !Recorded.insert(id.Value).second)
        return;
    Changes.push_back({ id, Document.CaptureEntity(entity), std::nullopt });
}

void EntityChangeRecorder::Created(EntityId entity)
{
    const PersistentEntityId id = IdOf(Document, entity);
    if (!id.IsValid() || !Recorded.insert(id.Value).second)
        return;
    Changes.push_back({ id, std::nullopt, std::nullopt });
}

EntityChangeSet EntityChangeRecorder::Finish() const
{
    EntityChangeSet set;
    for (const EntityChange& recorded : Changes)
    {
        EntityChange change = recorded;
        if (const EntityId entity = Resolve(Document, change.Id); entity.IsValid())
            change.After = Document.CaptureEntity(entity);
        const bool unchanged = change.Before && change.After && DigestOf(*change.Before) == DigestOf(*change.After);
        if ((change.Before || change.After) && !unchanged)
            set.Changes.push_back(std::move(change));
    }
    return set;
}

void ApplyEntityChangeSet(EditorDocument& document, const EntityChangeSet& changes, ChangeSetSide side)
{
    const auto target = [side](const EntityChange& change) -> const std::optional<EntitySnapshot>& {
        return side == ChangeSetSide::After ? change.After : change.Before;
    };

    // Overwrites and creations in recorded order, so a parent exists before the
    // children that name it; deletions after, children first.
    for (const EntityChange& change : changes.Changes)
    {
        const std::optional<EntitySnapshot>& wanted = target(change);
        if (!wanted)
            continue;
        if (const EntityId entity = Resolve(document, change.Id); entity.IsValid())
            document.OverwriteEntity(entity, *wanted);
        else
            (void)document.RestoreEntity(*wanted);
    }
    for (const EntityChange& change : std::views::reverse(changes.Changes))
    {
        if (target(change))
            continue;
        if (const EntityId entity = Resolve(document, change.Id); entity.IsValid())
            document.GetScene().DestroyEntity(entity);
    }
    document.GetScene().RefreshDerivedTransforms();
    document.MarkDirty();
}

std::vector<PersistentEntityId> ChangeSetConflicts(const EditorDocument& document,
                                                   const EntityChangeSet& changes)
{
    std::vector<PersistentEntityId> conflicts;
    for (const EntityChange& change : changes.Changes)
    {
        const EntityId entity = Resolve(document, change.Id);
        const bool unchanged = change.Before
            ? entity.IsValid() && DigestOf(document.CaptureEntity(entity)) == DigestOf(*change.Before)
            : !entity.IsValid();
        if (!unchanged)
            conflicts.push_back(change.Id);
    }
    return conflicts;
}
