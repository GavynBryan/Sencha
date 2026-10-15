#pragma once

#include "EntitySnapshot.h"

#include "scene_source/SceneSourceDocument.h"

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

class EditorDocument;

// What one operation or batch did to a document's entities, by persistent id:
// each entity's record before and after. No before is a creation, no after a
// deletion.
struct EntityChange
{
    PersistentEntityId Id;
    std::optional<EntitySnapshot> Before;
    std::optional<EntitySnapshot> After;
};

// A scene placed into a document, by its instance record. The entities the
// placement projects come and go with the record, never on their own.
struct InstanceChange
{
    SceneInstanceId Id;
    std::optional<SceneInstanceRecord> Before;
    std::optional<SceneInstanceRecord> After;
};

// An entity an edit relied on without changing it, as it was then: a door
// relies on its wall. Applying the change set elsewhere checks it is still so.
struct EntityRead
{
    PersistentEntityId Id;
    std::uint64_t      Digest = 0;
};

struct EntityChangeSet
{
    // In the order the entities were first touched: parents before children
    // for what was created.
    std::vector<EntityChange> Changes;
    std::vector<InstanceChange> Instances;
    std::vector<EntityRead> Reads;

    [[nodiscard]] bool Empty() const { return Changes.empty() && Instances.empty(); }
};

// Makes `into` the change from its before to `next`'s after, as if one edit
// had made both; an entity left as it was drops out.
void AppendChangeSet(EntityChangeSet& into, EntityChangeSet next);

// Records entity records around edits made directly to a document, so the
// edits become a change set that can be undone, redone and replayed.
class EntityChangeRecorder
{
public:
    explicit EntityChangeRecorder(EditorDocument& document);

    // Before the first edit to, or the deletion of, an entity that exists.
    void Touch(EntityId entity);
    // After creating an entity.
    void Created(EntityId entity);
    // After placing a scene.
    void Placed(SceneInstanceId instance);
    // An entity the edit relies on, as it is before the edit changes it.
    void Read(EntityId entity);

    // Every recorded entity that changed, as it is now; one deleted since has no after.
    [[nodiscard]] EntityChangeSet Finish() const;

private:
    EditorDocument& Document;
    std::vector<EntityChange> Changes;
    std::unordered_set<std::uint64_t> Recorded;
    std::vector<SceneInstanceId> PlacedInstances;
    std::vector<EntityRead> Reads;
};

enum class ChangeSetSide : std::uint8_t
{
    Before,
    After,
};

// Makes the document hold `side` of every change: overwriting in place what
// exists on both sides, creating what only that side has, destroying the rest.
// A placement is put back before the entity changes and taken away after
// them, so edits to what it projects apply over it.
void ApplyEntityChangeSet(EditorDocument& document, const EntityChangeSet& changes, ChangeSetSide side);

// The digest of the record `id` has in `document`; nullopt when nothing has it.
[[nodiscard]] std::optional<std::uint64_t> CurrentDigest(const EditorDocument& document, PersistentEntityId id);

// The entities whose current record is not the change set's before, or not
// what it read: changed, created or deleted since it was recorded. Applying
// over them would discard someone's edit or build on what is no longer there.
[[nodiscard]] std::vector<PersistentEntityId> ChangeSetConflicts(const EditorDocument& document,
                                                                 const EntityChangeSet& changes);
