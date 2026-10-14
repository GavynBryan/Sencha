#include "EntityChangeSet.h"

#include "EditorDocument.h"
#include "EditorScene.h"

#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <algorithm>
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

void EntityChangeRecorder::Placed(SceneInstanceId instance)
{
    if (std::ranges::find(PlacedInstances, instance) == PlacedInstances.end())
        PlacedInstances.push_back(instance);
}

EntityChangeSet EntityChangeRecorder::Finish() const
{
    EntityChangeSet set;
    for (const SceneInstanceId instance : PlacedInstances)
        if (const SceneInstanceRecord* record = Document.FindSceneInstance(instance))
            set.Instances.push_back({ instance, std::nullopt, *record });
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

namespace
{
    void ApplyInstances(EditorDocument& document, const EntityChangeSet& changes, ChangeSetSide side)
    {
        for (const InstanceChange& change : changes.Instances)
        {
            const std::optional<SceneInstanceRecord>& wanted = side == ChangeSetSide::After ? change.After : change.Before;
            if (document.FindSceneInstance(change.Id) != nullptr)
                (void)document.RemoveSceneInstance(change.Id);
            if (wanted)
                (void)document.RestoreSceneInstance(*wanted);
        }
    }
}

void ApplyEntityChangeSet(EditorDocument& document, const EntityChangeSet& changes, ChangeSetSide side)
{
    const auto target = [side](const EntityChange& change) -> const std::optional<EntitySnapshot>& {
        return side == ChangeSetSide::After ? change.After : change.Before;
    };

    if (side == ChangeSetSide::After)
        ApplyInstances(document, changes, side);
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
    if (side == ChangeSetSide::Before)
        ApplyInstances(document, changes, side);
    document.GetScene().RefreshDerivedTransforms();
    document.MarkDirty();
    document.AnnounceHandlesChanged();
}

std::optional<std::uint64_t> CurrentDigest(const EditorDocument& document, PersistentEntityId id)
{
    const EntityId entity = Resolve(document, id);
    if (!entity.IsValid())
        return std::nullopt;
    return DigestOf(document.CaptureEntity(entity));
}

std::vector<PersistentEntityId> ChangeSetConflicts(const EditorDocument& document,
                                                   const EntityChangeSet& changes)
{
    std::vector<PersistentEntityId> conflicts;
    for (const EntityChange& change : changes.Changes)
    {
        const std::optional<std::uint64_t> current = CurrentDigest(document, change.Id);
        const std::optional<std::uint64_t> before =
            change.Before ? std::optional<std::uint64_t>(DigestOf(*change.Before)) : std::nullopt;
        if (current != before)
            conflicts.push_back(change.Id);
    }
    for (const InstanceChange& change : changes.Instances)
        if ((document.FindSceneInstance(change.Id) != nullptr) != change.Before.has_value())
            conflicts.push_back(PersistentEntityId{ change.Id.Value });
    return conflicts;
}

void AppendChangeSet(EntityChangeSet& into, EntityChangeSet next)
{
    for (EntityChange& change : next.Changes)
    {
        const auto earlier = std::ranges::find(into.Changes, change.Id, &EntityChange::Id);
        if (earlier == into.Changes.end())
        {
            into.Changes.push_back(std::move(change));
            continue;
        }
        earlier->After = std::move(change.After);
        const bool never = !earlier->Before && !earlier->After;
        const bool unchanged = earlier->Before && earlier->After
            && DigestOf(*earlier->Before) == DigestOf(*earlier->After);
        if (never || unchanged)
            into.Changes.erase(earlier);
    }
    for (InstanceChange& change : next.Instances)
    {
        const auto earlier = std::ranges::find(into.Instances, change.Id, &InstanceChange::Id);
        if (earlier == into.Instances.end())
        {
            into.Instances.push_back(std::move(change));
            continue;
        }
        earlier->After = std::move(change.After);
        if (!earlier->Before && !earlier->After)
            into.Instances.erase(earlier);
    }
}
