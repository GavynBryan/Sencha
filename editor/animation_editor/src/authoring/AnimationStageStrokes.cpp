#include "authoring/AnimationStageStrokes.h"

#include "authoring/AnimationPreviewSession.h"

#include "EditorTheme.h"

#include <anim/Skeleton.h>

namespace
{
constexpr float kBoneWidth = 1.5f;
constexpr float kPathWidth = 2.0f;
constexpr float kWallWidth = 1.0f;
}

void AppendSkeletonStrokes(const SkeletonData& skeleton, std::span<const Mat4> model,
                           std::vector<EditorLineSegment>& out)
{
    const std::size_t count = std::min(skeleton.Joints.size(), model.size());
    for (std::size_t joint = 0; joint < count; ++joint)
    {
        const std::int32_t parent = skeleton.Joints[joint].ParentIndex;
        if (parent < 0 || static_cast<std::size_t>(parent) >= count)
            continue;
        out.push_back({ .A = model[static_cast<std::size_t>(parent)].TransformPoint(Vec3d::Zero()),
                        .B = model[joint].TransformPoint(Vec3d::Zero()),
                        .Color = EditorTheme::AnimationBone,
                        .WidthPx = kBoneWidth });
    }
}

void AppendPathStrokes(const AnimationPreviewSession& session, std::optional<AnimTick> shown,
                       std::vector<EditorLineSegment>& out)
{
    const auto line = [&out](const Vec3d& a, const Vec3d& b, const Vec4& color, float width) {
        out.push_back({ .A = a, .B = b, .Color = color, .WidthPx = width });
    };
    if (session.Scenario().Movement.has_value())
        for (const AnimationScenarioWall& wall : session.Scenario().Movement->Walls)
        {
            const Vec3d c = wall.Center;
            const Vec3d h = wall.HalfExtents;
            const auto corner = [&](int i) {
                return Vec3d(c.X + ((i & 1) ? h.X : -h.X), c.Y + ((i & 2) ? h.Y : -h.Y), c.Z + ((i & 4) ? h.Z : -h.Z));
            };
            for (int i = 0; i < 8; ++i)
                for (const int bit : { 1, 2, 4 })
                    if ((i & bit) == 0)
                        line(corner(i), corner(i | bit), EditorTheme::AnimationWall, kWallWidth);
        }

    const Vec3d feet(0.0f, session.SubjectHeight() * 0.5f, 0.0f);
    std::optional<Vec3d> previous;
    for (const AnimationPreviewTickRecord& record : session.History())
    {
        if (!record.Movement || (shown && record.Tick > *shown))
            continue;
        const Vec3d at = record.Movement->Position - feet;
        if (previous)
            line(*previous, at, EditorTheme::AnimationTrail, kPathWidth);
        if (record.Movement->Blocked)
        {
            const Vec3d from = at - record.Movement->Achieved;
            line(from, from + record.Movement->Requested, EditorTheme::AnimationOvershoot, kPathWidth);
        }
        previous = at;
    }
}
