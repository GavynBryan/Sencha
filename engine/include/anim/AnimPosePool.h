#pragma once

#include <anim/AnimTypes.h>
#include <anim/SkeletonHandle.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <span>
#include <vector>

// A decaying joint offset: translation along Direction and rotation about Axis,
// each a quintic from Distance or Angle and its speed to rest after its own seconds.
struct AnimJointOffset
{
    Vec3d Direction{};
    float Distance = 0.0f;
    float Speed = 0.0f;
    float Seconds = 0.0f;
    Vec3d Axis{};
    float Angle = 0.0f;
    float AngularSpeed = 0.0f;
    float AngleSeconds = 0.0f;
};

// Slots are assigned and shaped on the owner thread. The pose pass then writes each
// slot from one job, never resizing, so jobs over different entities share nothing.
class AnimPosePool
{
public:
    struct Slot
    {
        // Only the entity a slot was assigned to reads or releases it: pose state
        // copied onto another entity names a slot that is not that entity's.
        EntityId Owner;
        // What a renderer checks its mesh skins against before drawing.
        SkeletonHandle Skeleton;
        std::uint32_t Joints = 0;
        std::uint32_t Layers = 0;
        // Layers x Joints, layer-major.
        std::vector<AnimJointOffset> Offsets;
        std::vector<Transform3f> LayerPoses;
        std::vector<Transform3f> Current;
        std::vector<Transform3f> Previous;
        // Current's tick; Previous, when HasPrevious, is the tick before.
        AnimTick Tick = 0;
        bool HasCurrent = false;
        bool HasPrevious = false;
        bool Live = false;

        // Clears the slot when the shape changes.
        void Shape(std::uint32_t joints, std::uint32_t layers);

        [[nodiscard]] std::span<AnimJointOffset> LayerOffsets(std::size_t layer)
        {
            return std::span(Offsets).subspan(layer * Joints, Joints);
        }
        [[nodiscard]] std::span<Transform3f> LayerPose(std::size_t layer)
        {
            return std::span(LayerPoses).subspan(layer * Joints, Joints);
        }
        [[nodiscard]] std::span<const Transform3f> LayerPose(std::size_t layer) const
        {
            return std::span(LayerPoses).subspan(layer * Joints, Joints);
        }
    };

    // Empty until shaped. The handle is the slot index plus one.
    [[nodiscard]] std::uint32_t Allocate(EntityId owner);
    void Release(std::uint32_t handle, EntityId owner);

    // Null unless `handle` names a live slot assigned to `owner`.
    [[nodiscard]] Slot* Find(std::uint32_t handle, EntityId owner);
    [[nodiscard]] const Slot* Find(std::uint32_t handle, EntityId owner) const;
    [[nodiscard]] std::size_t LiveCount() const { return Slots.size() - Free.size(); }

private:
    std::vector<Slot> Slots;
    std::vector<std::uint32_t> Free;
};
