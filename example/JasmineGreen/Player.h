#pragma once

#include <core/batch/DataBatchKey.h>
#include <math/geometry/2d/Transform2d.h>
#include <world/transform/TransformNode.h>
#include <world/transform/TransformSpace.h>

//=============================================================================
// Player
//
// The playable character. Owns a Body transform that drives movement and
// participates in the EntityRegistry. Visual representation lives in
// SpriteComponents stored externally and parented to Body.TransformKey().
//=============================================================================
struct Player
{
    static constexpr float MoveSpeed          = 200.0f; // Pixels per second
    static constexpr float CollisionHalfExtent = 24.0f; // Half of the 48px body

    TransformNode2d Body;

    Player(TransformSpace2d& domain, Transform2f spawnTransform)
        : Body(domain, spawnTransform)
    {}

    DataBatchKey TransformKey() const { return Body.TransformKey(); }
};
