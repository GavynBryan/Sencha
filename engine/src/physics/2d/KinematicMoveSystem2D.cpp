#include <physics/2d/KinematicMoveSystem2D.h>

#include <algorithm>

KinematicMoveSystem2D::KinematicMoveSystem2D(
    DataBatch<CharacterMotor2D>& motors,
    DataBatchKey motorKey,
    TransformView<Transform2f>& transforms,
    DataBatchKey transformKey,
    PhysicsDomain2D& physics,
    const ColliderSyncSystem2D::PhysicsToken& colliderToken,
    ColliderSyncSystem2D& syncSystem)
    : Motors(motors)
    , MotorKey(motorKey)
    , Transforms(transforms)
    , TransformKey(transformKey)
    , Physics(physics)
    , ColliderToken(colliderToken)
    , SyncSystem(syncSystem)
{}

void KinematicMoveSystem2D::Update(const FrameTime& time)
{
    CharacterMotor2D* motor = Motors.TryGet(MotorKey);
    if (!motor) return;

    Transform2f* local = Transforms.TryGetLocalMutable(TransformKey);
    if (!local) return;

    const Collider2D* collider = SyncSystem.TryGetCollider(ColliderToken);
    if (!collider) return;

    const float dt = time.DeltaTime;

    // -- 1. Jump ---------------------------------------------------------------
    if (motor->JumpRequested && motor->Grounded)
    {
        motor->VerticalSpeed = motor->JumpSpeed;
        motor->Grounded      = false;
    }
    motor->JumpRequested = false;

    // -- 2. Gravity (only when airborne) ---------------------------------------
    if (!motor->Grounded)
        motor->VerticalSpeed -= motor->Gravity * dt;

    // -- 3. Build desired delta ------------------------------------------------
    const Vec2d desiredDelta = {
        motor->DesiredMoveX * motor->MoveSpeed * dt,
        motor->VerticalSpeed * dt
    };

    // -- 4. Resolve collision --------------------------------------------------
    MoveResult2D result = Physics.MoveBox(collider->WorldBounds, desiredDelta);

    // -- 5. Write resolved position --------------------------------------------
    local->Position.X += result.ResolvedDelta.X;
    local->Position.Y += result.ResolvedDelta.Y;

    // -- 6. Update grounded state ----------------------------------------------
    motor->Grounded = result.HitFloor;

    // -- 7. Squash vertical speed on surface contact ---------------------------
    if (result.HitFloor || result.HitCeiling)
        motor->VerticalSpeed = 0.0f;
}
