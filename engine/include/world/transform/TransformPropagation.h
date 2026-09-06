#pragma once

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <world/transform/PropagationOrderCache.h>
#include <world/transform/TransformComponents.h>

// Computes world transforms for the phase's active storage partitions.
//
// Two passes, because the two populations have different constraints. Entities
// with no parent have no ordering constraint at all, so they are swept
// chunk-linearly with the dirty test applied once per chunk. Entities with a
// parent are swept through a cached parent-before-child order, which
// PropagationOrderCache maintains and invalidates against the hierarchy rather
// than against every structural change.
//
// A partition that re-enters a domain receives one conservative full sweep;
// dormant partitions perform no propagation work.
//
// In the Presentation domain a child whose parent carries WorldTransformHistory
// composes from the parent's pose interpolated at `presentationAlpha`, the same
// pose mesh extraction draws the parent at. That is what keeps a camera or a
// held object attached to a body drawn where the body is drawn: simulation runs
// at a fixed rate the display does not share, and a child that inherited the
// tick pose would step while its parent glides. Such children recompute every
// presentation sweep, because the blend changes even when nothing moved. The
// Simulation domain ignores the alpha and composes from tick poses.
//
// forceFullInvalidation rebuilds the order every sweep. It exists so a
// suspected stale-transform bug can be bisected against the scoped
// invalidation in one step, and so a churn scenario can be run both ways and
// compared. It is not a fast path.
void PropagateTransforms(
    World& world,
    const StoragePartitionSet& partitions,
    TransformPropagationDomain domain,
    bool forceFullInvalidation = false,
    double presentationAlpha = 1.0);

// Test/tool convenience for an isolated World where every live partition is
// intentionally active. Runtime frame code must pass the explicit phase-domain
// set above so dormant zones remain skipped at chunk granularity.
void PropagateTransforms(World& world);
