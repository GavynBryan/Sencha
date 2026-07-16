#pragma once

#include <ecs/Query.h>
#include <ecs/World.h>
#include <world/transform/PropagationOrderCache.h>
#include <world/transform/TransformComponents.h>

#include <span>

class JobSystem;
class Registry;

//=============================================================================
// TransformPropagationSystem
//
// Propagates LocalTransform → WorldTransform for all entities that have both
// components, respecting the spatial hierarchy expressed by the Parent component.
//
// Uses a PropagationOrderCache World resource: a parent-before-child dense
// ordered list rebuilt when World::StructuralVersion() moves (any entity
// create/destroy or component add/remove can relocate rows) or when
// Changed<Parent> signals a hierarchy edit. Each frame the sweep is a single
// forward pass with no hash lookups that recomputes only dirty subtrees
// (local transform changed, or an ancestor was recomputed).
//
// See docs/ecs/decisions.md D3.1 and D4.4 for mandate and benchmark rationale.
//=============================================================================
class TransformPropagationSystem
{
public:
    TransformPropagationSystem(World& world, PropagationOrderCache& cache)
        : Target(world)
        , Cache(cache)
    {
    }

    void Propagate();

    void Tick(float /*fixedDt*/)
    {
        Propagate();
    }

private:
    World& Target;
    PropagationOrderCache& Cache;

    // Rebuilds the order cache from the current Parent graph.
    // Called when the cache is dirty (Changed<Parent> fired, or first frame).
    void RebuildCache();
};

inline void PropagateTransforms(World& world, PropagationOrderCache& cache)
{
    TransformPropagationSystem propagation(world, cache);
    propagation.Propagate();
}

// Restores world-transform coherence for every unique registry in the span.
void PropagateTransforms(std::span<Registry*> registries);

// Zone-parallel variant (docs/ecs/parallelization.md, Stage C): one job per
// unique registry. Legal because propagation is per-World pure — the order
// cache is a World resource and parent-before-child ordering is an intra-zone
// constraint, so zones propagate independently. Results are bit-identical to
// the serial overload: the per-zone work is unchanged, only which thread runs
// it differs.
void PropagateTransforms(JobSystem& jobs, std::span<Registry*> registries);
