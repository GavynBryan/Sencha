#include "authoring/AbilityStageStrokes.h"

#include "authoring/AbilityPreviewSession.h"

#include "EditorTheme.h"
#include "render/LineShapes.h"

#include <controller/Aim.h>
#include <ecs/World.h>
#include <physics/components/CharacterController.h>
#include <world/transform/TransformComponents.h>

#include <optional>

namespace
{
constexpr float kFloorHalfSize = 10.0f;
constexpr float kFloorSpacing = 1.0f;
constexpr float kAimLength = 3.0f;
constexpr float kHitNormalLength = 0.5f;
constexpr float kHitCross = 0.15f;
constexpr float kFloorWidth = 1.0f;
constexpr float kShapeWidth = 1.5f;
constexpr float kTrailWidth = 2.0f;
}

AbilityStage AbilityStageOf(const AbilityPreviewSession& session, std::uint64_t shown)
{
    AbilityStage stage;
    const World* world = session.SimulationWorld();
    const CharacterController* capsule = session.FixtureCapsule();
    if (world == nullptr || capsule == nullptr)
        return stage;
    std::vector<EditorLineSegment>& out = stage.Strokes;
    const Vec3d feet(0.0f, capsule->Height * 0.5f, 0.0f);

    const AbilityPreviewTickRecord* last = nullptr;
    std::optional<Vec3d> previous;
    for (const AbilityPreviewTickRecord& record : session.History())
    {
        if (record.Tick > shown)
            break;
        const Vec3d at = record.Pose.Position - feet;
        if (previous)
            out.push_back({ *previous, at, EditorTheme::AbilityTrail, kTrailWidth });
        previous = at;
        stage.Bounds.ExpandToInclude(at);
        for (const Vec3d& placed : record.Placed)
        {
            const Aabb3d box = Aabb3d::FromCenterHalfExtent(
                placed, Vec3d(AbilityPreviewSession::kTargetHalfExtent, AbilityPreviewSession::kTargetHalfExtent,
                              AbilityPreviewSession::kTargetHalfExtent));
            AppendBoxEdges(out, box, EditorTheme::AbilityTarget, kShapeWidth);
            stage.Bounds.ExpandToInclude(box);
        }
        last = &record;
    }

    const Transform3f pose =
        last != nullptr ? last->Pose : world->TryGet<LocalTransform>(session.Fixture())->Value;
    const Aim aim = last != nullptr ? Aim{ last->AimOrigin, last->AimDirection } : AimOf(*world, session.Fixture());
    AppendFloorGrid(out, Vec3d(pose.Position.X, 0.0f, pose.Position.Z), kFloorHalfSize, kFloorSpacing,
                    EditorTheme::AbilityFloor, kFloorWidth);
    AppendCapsuleEdges(out, pose.Position, capsule->Radius, capsule->Height, EditorTheme::AbilityPawn, kShapeWidth);
    out.push_back({ aim.Origin, aim.Origin + aim.Direction * kAimLength, EditorTheme::AbilityAim, kShapeWidth });
    stage.Bounds.ExpandToInclude(pose.Position - feet);
    stage.Bounds.ExpandToInclude(pose.Position + feet);
    stage.Bounds.ExpandToInclude(aim.Origin + aim.Direction * kAimLength);
    if (last != nullptr)
        for (const AbilityPreviewHit& hit : last->Hits)
        {
            out.push_back({ hit.Point, hit.Point + hit.Normal * kHitNormalLength, EditorTheme::AbilityHit, kShapeWidth });
            for (const Vec3d& axis : { Vec3d(kHitCross, 0.0f, 0.0f), Vec3d(0.0f, kHitCross, 0.0f),
                                       Vec3d(0.0f, 0.0f, kHitCross) })
                out.push_back({ hit.Point - axis, hit.Point + axis, EditorTheme::AbilityHit, kShapeWidth });
            stage.Bounds.ExpandToInclude(hit.Point);
        }
    return stage;
}
