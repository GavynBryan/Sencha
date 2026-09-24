#pragma once

#include <anim/AnimTypes.h>
#include <anim/SkeletonHandle.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// AnimPosePool
//
// The per-joint pose storage of every posed entity, a World resource: what is
// too large and too variable for a fixed-size component. A slot holds, for
// its entity's skeleton,
//
//   - per layer, the joint offsets an inertialized change is still decaying
//     and the layer's own pose this tick, before composition;
//   - the composed local pose of this tick and the one before, which render
//     extraction interpolates between.
//
// Slots are assigned and shaped on the owner thread. The pose pass then writes
// each slot from one job at a time, never resizing, so jobs over different
// entities never share storage.
//=============================================================================

// A joint's decaying offset from the pose it blends into: a translation along
// Direction and a rotation about Axis, each following a quintic in time that
// starts at Distance or Angle with the given speed and reaches rest, with no
// velocity or acceleration, after its own number of seconds.
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

class AnimPosePool
{
public:
    struct Slot
    {
        EntityId Owner;
        // The skeleton the poses are of: what a renderer checks its mesh
        // skins before drawing them.
        SkeletonHandle Skeleton;
        std::uint32_t Joints = 0;
        std::uint32_t Layers = 0;
        // Layers x Joints, layer-major.
        std::vector<AnimJointOffset> Offsets;
        std::vector<Transform3f> LayerPoses;
        std::vector<Transform3f> Current;
        std::vector<Transform3f> Previous;
        // The tick Current was posed on; Previous is the tick before, when
        // HasPrevious.
        AnimTick Tick = 0;
        bool HasCurrent = false;
        bool HasPrevious = false;
        bool Live = false;

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

    // A slot for `owner`, empty until shaped. Returns the slot's handle, which
    // is its index plus one.
    [[nodiscard]] std::uint32_t Allocate(EntityId owner);
    void Release(std::uint32_t handle);
    // Sizes the slot for a skeleton and layer count, clearing whatever it held
    // when the shape changes.
    void Shape(std::uint32_t handle, std::uint32_t joints, std::uint32_t layers);

    [[nodiscard]] Slot* Find(std::uint32_t handle);
    [[nodiscard]] const Slot* Find(std::uint32_t handle) const;
    [[nodiscard]] std::size_t LiveCount() const { return Slots.size() - Free.size(); }

private:
    std::vector<Slot> Slots;
    std::vector<std::uint32_t> Free;
};
