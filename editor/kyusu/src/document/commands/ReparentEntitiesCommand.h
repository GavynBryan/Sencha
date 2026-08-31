#pragma once

#include "commands/ICommand.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <memory>
#include <span>
#include <vector>

class EditorDocument;
class EditorScene;

// How a reparent treats the child's transform. KeepWorld re-expresses the
// child in the new parent's frame so it does not move on screen -- the default,
// because the gesture is about the relationship, not about placement. KeepLocal
// carries the authored local value into the new frame, snapping the child to
// wherever that lands: what assembling a kit from parts authored at the origin
// wants.
enum class ReparentTransformRule : std::uint8_t
{
    KeepWorld,
    KeepLocal,
};

// Reparents a set of entities under one new parent (invalid parent = the scene
// root) as a single undoable step. Undo restores each entity's previous parent
// and its exact previous local transform rather than recomputing it, so a
// round trip is bit-identical even where the conversion math would not be.
//
// With `reorder`, the entities are also moved in the scene's tracked order:
// the block lands immediately before `insertBefore`, or at the end when that
// is invalid -- the between-rows half of the hierarchy drop gesture. Entities
// already under `newParent` then stay in the set (their order is the change)
// but skip the transform work, so a pure reorder cannot drift a transform.
class ReparentEntitiesCommand : public ICommand
{
public:
    ReparentEntitiesCommand(std::span<const EntityId> entities, EntityId newParent,
                            ReparentTransformRule rule,
                            EditorScene& scene, EditorDocument& document,
                            bool reorder = false, EntityId insertBefore = {});

    void Execute() override;
    void Undo() override;

private:
    struct Entry
    {
        EntityId Entity;
        EntityId PreviousParent;
        Transform3f PreviousLocal = Transform3f::Identity();
        Transform3f World = Transform3f::Identity();
    };

    EditorScene&    Scene;
    EditorDocument& Document;
    EntityId        NewParent;
    ReparentTransformRule Rule;
    std::vector<Entry> Entries;
    bool     Reorder;
    EntityId InsertBefore;
    std::vector<EntityId> PreviousOrder;
    bool Captured = false;
};

// Builds the reparent, or nullptr when nothing would change. Filters the input
// down to what the gesture means: entities already under `newParent` drop out
// as no-ops, an entity whose ancestor is also in the set drops out because it
// rides along with that ancestor, and any target that would create a cycle
// (the parent is the entity itself or one of its descendants) disqualifies the
// whole gesture rather than half-applying it.
[[nodiscard]] std::unique_ptr<ICommand> MakeReparentEntitiesCommand(
    std::span<const EntityId> entities, EntityId newParent,
    ReparentTransformRule rule, EditorScene& scene, EditorDocument& document,
    bool reorder = false, EntityId insertBefore = {});
