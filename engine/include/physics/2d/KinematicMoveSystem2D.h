#pragma once

#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <core/system/ISystem.h>
#include <math/geometry/2d/Transform2d.h>
#include <physics/2d/CharacterMotor2D.h>
#include <physics/2d/ColliderSyncSystem2D.h>
#include <physics/2d/PhysicsDomain2D.h>
#include <world/transform/TransformView.h>

//=============================================================================
// KinematicMoveSystem2D
//
// Executes one kinematic move per frame for a single CharacterMotor2D:
//
//   1. Apply gravity to VerticalSpeed (skip if grounded)
//   2. Apply jump impulse if JumpRequested and grounded
//   3. Build desired delta from DesiredMoveX * MoveSpeed + VerticalSpeed
//   4. Ask PhysicsDomain2D::MoveBox for the safe resolved delta
//   5. Write resolved position back to Transform2D local position
//   6. Update Grounded flag from HitFloor
//   7. Zero VerticalSpeed if HitFloor or HitCeiling
//
// Runs at SystemPhase::PostUpdate, after ColliderSyncSystem2D has rebuilt
// the physics grid. The transform write here will be propagated in the
// next frame's TransformPropagationSystem pass. If same-frame propagation
// is required, call TransformPropagationSystem::Propagate() manually after
// this system runs.
//
// The collider for the moving entity must be registered in
// ColliderSyncSystem2D before this system runs. KinematicMoveSystem2D reads
// the collider's current WorldBounds (already synced this frame) as the
// starting AABB for the move query.
//=============================================================================
class KinematicMoveSystem2D : public ISystem
{
public:
    // motors        : batch owning CharacterMotor2D components
    // motorKey      : key of the player's motor in the batch
    // transforms    : the world's transform view (for writing local position)
    // transformKey  : transform key of the player entity
    // physics       : the physics domain to query
    // colliderToken : the player's registered collider (for AABB source)
    // syncSystem    : needed to read current WorldBounds from the collider token
    KinematicMoveSystem2D(DataBatch<CharacterMotor2D>& motors,
                          DataBatchKey motorKey,
                          TransformView<Transform2f>& transforms,
                          DataBatchKey transformKey,
                          PhysicsDomain2D& physics,
                          const ColliderSyncSystem2D::PhysicsToken& colliderToken,
                          ColliderSyncSystem2D& syncSystem);

private:
    void Update(const FrameTime& time) override;

    DataBatch<CharacterMotor2D>& Motors;
    DataBatchKey                  MotorKey;
    TransformView<Transform2f>&  Transforms;
    DataBatchKey                  TransformKey;
    PhysicsDomain2D&              Physics;
    const ColliderSyncSystem2D::PhysicsToken& ColliderToken;
    ColliderSyncSystem2D&         SyncSystem;
};
