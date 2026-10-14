#pragma once

#include "document/LevelConventions.h"
#include "operations/EditorOperations.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

class EditorDocument;
class EditorScene;
class EntityChangeRecorder;

// The edits on spaces that operations and Kyusu's tools both make, so a
// designer and an agent get one result from one request (decision D5). Each
// edits `document` directly and tells `recorder` what it touches; the caller
// runs it as one mutation.

[[nodiscard]] LevelConventions ConventionsOf(const EditorDocument& document);

struct NewSpace
{
    std::string          Name;
    Vec3d                Min;
    Vec3d                Max;
    bool                 Shelled = true;
    std::optional<float> Thickness; // the conventions' when absent
};

// A box space between the corners, named, painted with the conventions'
// floor, ceiling and wall materials, and shelled. Refused for an empty or taken
// name, or a box under 0.1 m on any axis.
[[nodiscard]] EditorOperationOutcome CreateSpace(EditorDocument& document, EntityChangeRecorder& recorder,
                                                 const NewSpace& space, EntityId& created);

struct EntityMove
{
    EntityId    Entity;
    Transform3f World;
};

struct MoveReport
{
    std::vector<EntityId> Carried; // spaces whose face was pulled along
    std::vector<EntityId> Skipped; // spaces left alone because their walls were detached
};

// Places each entity at its world transform. With carry, a space that only
// moves pulls the face of the space each door it owns opens onto by the move's
// part along that face, unless that space is moving too. Refused, having
// changed things the caller must put back, when a pull would turn a space
// inside out.
[[nodiscard]] EditorOperationOutcome MoveEntities(EditorDocument& document, EntityChangeRecorder& recorder,
                                                  std::span<const EntityMove> moves, bool carry, MoveReport& report);

// Moves one face of `entity`'s volume along the world `delta`. False when the
// result would not be a usable brush, or would turn any face inside out.
[[nodiscard]] bool MoveFace(EditorScene& scene, EntityId entity, std::uint32_t face, const Vec3d& delta);
