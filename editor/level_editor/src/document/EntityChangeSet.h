#pragma once

#include "EntitySnapshot.h"

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

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

struct EntityChangeSet
{
    // In the order the entities were first touched: parents before children
    // for what was created.
    std::vector<EntityChange> Changes;

    [[nodiscard]] bool Empty() const { return Changes.empty(); }
};

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

    // Every recorded entity as it is now; one deleted since has no after.
    [[nodiscard]] EntityChangeSet Finish() const;

private:
    EditorDocument& Document;
    std::vector<EntityChange> Changes;
    std::unordered_set<std::uint64_t> Recorded;
};

enum class ChangeSetSide : std::uint8_t
{
    Before,
    After,
};

// Makes the document hold `side` of every change: overwriting in place what
// exists on both sides, creating what only that side has, destroying the rest.
void ApplyEntityChangeSet(EditorDocument& document, const EntityChangeSet& changes, ChangeSetSide side);

// The entities whose current record is not the change set's before: changed,
// created or deleted since it was recorded. Applying over them would discard
// someone's edit.
[[nodiscard]] std::vector<PersistentEntityId> ChangeSetConflicts(const EditorDocument& document,
                                                                 const EntityChangeSet& changes);
