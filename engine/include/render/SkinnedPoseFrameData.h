#pragma once

#include <graphics/BufferHandle.h>
#include <math/Mat.h>
#include <render/RenderEntityKey.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>

#include <cstdint>
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
// the forward pass (MainColor, recorded after Offscreen) binds them. Ready
// says the buffers are valid for this frame -- when false, items fall back
// to their rest geometry, which is what a skinned mesh drew before a pose
// path existed.
//=============================================================================

// Palettes start on a boundary of this many matrices. The pose dispatch hands
// each palette's byte offset to a storage-buffer descriptor, which must be a
// multiple of the device's minStorageBufferOffsetAlignment; Vulkan caps that
// limit at 256 bytes, so four 64-byte matrices satisfy every conformant
// device without querying one. Packing tightly instead would leave any
// instance whose predecessors' joints do not sum to a multiple of four on an
// illegal offset.
inline constexpr std::uint32_t kPaletteJointAlignment = 4;

struct SkinnedPoseInstance
{
    SkinnedMeshHandle Mesh;
    // Stable per entity: keys the instance's retained posed-buffer slots.
    RenderEntityKey Key;
    // The instance's palette inside Palettes: JointCount matrices starting
    // at PaletteOffset (element index, not bytes), aligned per above.
    std::uint32_t PaletteOffset = 0;
    std::uint32_t JointCount = 0;
};

struct SkinnedPoseFrameData
{
    bool Ready = false;
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
        Ready = false;
        Instances.clear();
        Palettes.clear();
        PosedBuffers.clear();
    }
};
