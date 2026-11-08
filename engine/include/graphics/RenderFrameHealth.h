#pragma once

#include <graphics/GpuFrameScratch.h>

#include <cstdint>

// What the last recorded frame could not do. Filled in every build and at every
// render.profile.mode, so a host can tell a fast frame from an empty one.
struct RenderFrameHealth
{
    // Advances once per recorded frame; a frame that never recorded leaves this alone.
    std::uint64_t FramesRecorded = 0;

    std::uint32_t ScratchFailures = 0;
    std::uint32_t ScratchTagFailures[ScratchTagCounters::kTagCount]{};
    std::uint32_t PassesSkipped = 0;
    std::uint32_t InstancesDropped = 0;
    std::uint32_t ShadowCastersDropped = 0;

    std::uint32_t SkinnedFirstPose = 0;
    std::uint32_t SkinnedRefreshed = 0;
    std::uint32_t SkinnedHeld = 0;
    std::uint32_t SkinnedAwaitingFirstPose = 0;
    std::uint32_t SkinnedBindFallback = 0;
    std::uint32_t SkinnedPoseFailures = 0;
    std::uint32_t PosedBuffers = 0;
    std::uint64_t PosedBufferBytes = 0;

    [[nodiscard]] bool LostWork() const
    {
        return ScratchFailures != 0 || PassesSkipped != 0 || InstancesDropped != 0
            || ShadowCastersDropped != 0 || SkinnedBindFallback != 0 || SkinnedPoseFailures != 0;
    }
};
