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

struct SkinnedPoseInstance
{
    SkinnedMeshHandle Mesh;
    // Stable per entity: keys the instance's retained posed-buffer slots.
    RenderEntityKey Key;
    // The instance's palette inside Palettes: JointCount matrices starting
    // at PaletteOffset (element index, not bytes).
    std::uint32_t PaletteOffset = 0;
    std::uint32_t JointCount = 0;
};

struct SkinnedPoseFrameData
{
    bool Ready = false;
    std::vector<SkinnedPoseInstance> Instances;
    std::vector<Mat4> Palettes;
    std::vector<BufferHandle> PosedBuffers;

    void Reset()
    {
        Ready = false;
        Instances.clear();
        Palettes.clear();
        PosedBuffers.clear();
    }
};
