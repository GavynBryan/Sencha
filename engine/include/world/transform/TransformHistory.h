#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

class StoragePartitionSet;
class World;

//=============================================================================
// WorldTransformHistory
//
// The two most recent simulated poses of an entity, so rendering can show
// where it is between ticks instead of snapping to the last completed one.
// Simulation runs at a fixed rate that has nothing to do with the display's,
// and a frame usually lands part way through a tick; that fraction arrives as
// PresentationTime::Alpha.
//
// Opt-in per entity. Entities without it extract their live WorldTransform
// exactly as before, which is what keeps this off the cost of static geometry
// and out of editor worlds entirely (it is runtime-only and never serialized).
//
// Snap means "there is no previous pose to blend from": a freshly spawned or
// streamed-in entity, or one that was teleported. Blending across a teleport
// would draw the entity streaking through the space between.
//
// Mesh and shadow-caster extraction consume this; they have to agree or a
// shadow separates from the object casting it. Light extraction does not, so a
// light on a history-carrying entity still steps at the tick rate.
//=============================================================================
struct WorldTransformHistory
{
    Transform3f Previous;
    Transform3f Current;
    bool Snap = true;
};
SENCHA_DECLARE_COMPONENT_TYPE(WorldTransformHistory, "sencha.world_transform_history");

// Roll Current into Previous and sample the entity's world pose. Call once per
// fixed tick, after simulation-domain propagation has produced this tick's
// WorldTransform. snapAll collapses every history, for a frame-wide
// discontinuity where no entity's previous pose is meaningful any more.
void CaptureWorldTransformHistory(World& world,
                                  const StoragePartitionSet& partitions,
                                  bool snapAll);

// Collapse one entity's history, for a teleport that the rest of the frame
// should not blend through. Safe to call on an entity without history.
void RequestTransformHistorySnap(World& world, EntityId entity);

// The pose to render: the blend between the last two ticks, or the current
// pose when there is nothing meaningful to blend from.
[[nodiscard]] Transform3f ResolvePresentationPose(const WorldTransformHistory& history,
                                                  double alpha);
