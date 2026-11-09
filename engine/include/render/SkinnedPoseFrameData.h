#pragma once

#include <graphics/BufferHandle.h>
#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/AabbTransform.h>
#include <render/RenderEntityKey.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// SkinnedPoseFrameData
//
// The frame's skinned instances and their joint palettes, and -- once the
// pose feature has dispatched -- the posed vertex buffer each one draws
// from. One entry per skinned ENTITY (its queue items all share a pose), in
// extraction order; a queue item's PoseSlot indexes Instances/PosedBuffers.
//
// Extraction fills Instances and Palettes on the owner thread; the pose
// feature (Offscreen) acquires buffers, dispatches, and fills PosedBuffers;
// the forward pass (MainColor, recorded after Offscreen) binds them. An
// instance with no posed buffer draws its rest geometry.
//=============================================================================

// Palettes start on a boundary of this many matrices. The pose dispatch hands
// each palette's byte offset to a storage-buffer descriptor, which must be a
// multiple of the device's minStorageBufferOffsetAlignment; Vulkan caps that
// limit at 256 bytes, so four 64-byte matrices satisfy every conformant
// device without querying one. Packing tightly instead would leave any
// instance whose predecessors' joints do not sum to a multiple of four on an
// illegal offset.
inline constexpr std::uint32_t kPaletteJointAlignment = 4;

// Floats per palette matrix as the pose shader reads it.
inline constexpr std::size_t kPaletteMatrixFloats = 16;

// The pose shader's std430 `mat4` is column-major and Mat4 is row-major, so
// each matrix is transposed. `out` holds kPaletteMatrixFloats per matrix.
inline void CopyPalettesColumnMajor(std::span<const Mat4> palettes, std::span<float> out)
{
    for (std::size_t m = 0; m < palettes.size() && (m + 1) * kPaletteMatrixFloats <= out.size(); ++m)
    {
        float* column = out.data() + m * kPaletteMatrixFloats;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                column[col * 4 + row] = palettes[m].Data[row][col];
    }
}

// Bounds holding a mesh under a pose, from its rest bounds and joint palette. Skinning
// blends a vertex between its joints' transforms, so it stays inside the union of the
// rest bounds under each joint. Conservative, and a rest-bounds box is not: a posed limb
// reaches outside it.
[[nodiscard]] inline Aabb3d PosedWorldBounds(const Aabb3d& localBounds, const Mat4& world,
                                             std::span<const Mat4> palette)
{
    Aabb3d bounds = Aabb3d::Empty();
    for (const Mat4& joint : palette)
        bounds.ExpandToInclude(TransformAabb(localBounds, world * joint));
    return bounds;
}

struct SkinnedPoseInstance
{
    SkinnedMeshHandle Mesh;
    // Stable per entity: keys the instance's retained posed-buffer slots.
    RenderEntityKey Key;
    // The instance's palette inside Palettes: JointCount matrices starting
    // at PaletteOffset (element index, not bytes), aligned per above.
    std::uint32_t PaletteOffset = 0;
    std::uint32_t JointCount = 0;
    float CameraDepth = 0.0f;
};

// Every visible skinned instance lands in exactly one of the first five.
struct SkinnedPoseOutcome
{
    std::uint32_t FirstPose = 0;
    std::uint32_t Refreshed = 0;
    std::uint32_t Held = 0;
    std::uint32_t AwaitingFirstPose = 0;
    std::uint32_t BindFallback = 0;
    // Chosen instances whose dispatch could not be recorded, already counted above.
    std::uint32_t PoseFailures = 0;

    [[nodiscard]] std::uint32_t Visible() const
    {
        return FirstPose + Refreshed + Held + AwaitingFirstPose + BindFallback;
    }
};

struct SkinnedPoseFrameData
{
    std::vector<SkinnedPoseInstance> Instances;
    std::vector<Mat4> Palettes;
    std::vector<BufferHandle> PosedBuffers;

    // Appends one instance and reserves its palette at the next aligned start,
    // identity-filled -- identity is the bind pose, so an instance nothing
    // poses draws its rest geometry. Any matrices skipped for alignment are
    // filled too, so no dispatch ever reads uninitialized scratch. Returns the
    // pose slot, which is what a queue item carries.
    std::uint32_t AppendInstance(SkinnedMeshHandle mesh, RenderEntityKey key,
                                 std::uint32_t jointCount)
    {
        const auto slot = static_cast<std::uint32_t>(Instances.size());
        const auto start = static_cast<std::uint32_t>(
            (Palettes.size() + kPaletteJointAlignment - 1)
            / kPaletteJointAlignment * kPaletteJointAlignment);
        Instances.push_back(SkinnedPoseInstance{
            .Mesh = mesh,
            .Key = key,
            .PaletteOffset = start,
            .JointCount = jointCount,
        });
        Palettes.resize(start + jointCount, Mat4::Identity());
        return slot;
    }

    void Reset()
    {
        Instances.clear();
        Palettes.clear();
        PosedBuffers.clear();
    }
};
