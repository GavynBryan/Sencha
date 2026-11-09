#pragma once

#include <graphics/FramesInFlight.h>
#include <graphics/vulkan/SkinnedPosePass.h>
#include <render/PosedBufferVersions.h>
#include <render/SkinnedPoseFrameData.h>
#include <render/SkinnedPoseSelection.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>

#include <core/logging/RepeatedFailureLog.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

//=============================================================================
// SkinnedPoseRenderFeature
//
// Offscreen feature that turns the frame's skinned instances into posed
// vertex buffers. Each frame it chooses which instances to pose within the
// budget (docs/renderer/skinning.md), dispatches those, and points the rest at
// their latest finished pose. Holds the backend pass by value, which is why
// this header is on the render-isolation allowlist.
//=============================================================================
struct RenderFrameHealth;

class SkinnedPoseRenderFeature : public IRenderFeature
{
public:
    SkinnedPoseRenderFeature(std::shared_ptr<SkinnedPoseFrameData> frameData,
                             const SkinnedMeshCache& skinnedMeshes,
                             SkinnedPoseBudget budget);

    [[nodiscard]] RenderPhase GetPhase() const override
    {
        return RenderPhase::Offscreen;
    }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

private:
    struct PosedSlots
    {
        RenderEntityKey Key;
        std::uint32_t VertexCount = 0;
        std::array<BufferHandle, kMaxFramesInFlight> Buffers{};
        PosedBufferVersions Versions;
        std::uint32_t LastSeenFrame = 0;
        std::uint32_t LastDispatchFrame = 0;
    };

    struct DispatchTarget
    {
        std::uint32_t Instance = 0;
        std::uint32_t Version = 0;
    };

    [[nodiscard]] PosedSlots& SlotFor(const RenderEntityKey& key);
    void ReleaseBuffers(PosedSlots& slots);
    void PruneStale();
    void RecordPoses(const RenderFrame& frame, SkinnedPoseOutcome& outcome);
    void ResolveUnposed(const RenderFrame& frame, SkinnedPoseOutcome& outcome);
    void PublishOutcome(const SkinnedPoseOutcome& outcome);

    std::shared_ptr<SkinnedPoseFrameData> FrameData;
    const SkinnedMeshCache* SkinnedMeshes = nullptr;
    SkinnedPoseBudget Budget;
    GpuFrameScratch* Scratch = nullptr;
    const RenderInstrumentation* Instrumentation = nullptr;
    RenderFrameHealth* Health = nullptr;
    Logger* Log = nullptr;
    RepeatedFailureLog PoseFailureLog;
    RepeatedFailureLog OverBudgetLog;
    SkinnedPosePass Pass;
    // Sorted by Key.
    std::vector<PosedSlots> Slots;
    std::vector<SkinnedPoseCandidate> Candidates;
    std::vector<std::uint32_t> Chosen;
    std::vector<std::uint8_t> Attempted;
    std::vector<SkinnedPoseDispatch> Dispatches;
    std::vector<DispatchTarget> DispatchTargets;
    std::vector<std::uint8_t> Recorded;
    std::uint32_t FrameCounter = 0;
};
