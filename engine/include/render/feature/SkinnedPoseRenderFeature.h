#pragma once

#include <graphics/FramesInFlight.h>
#include <graphics/vulkan/SkinnedPosePass.h>
#include <render/SkinnedPoseFrameData.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

//=============================================================================
// SkinnedPoseRenderFeature
//
// Offscreen feature that turns the frame's skinned instances into posed
// vertex buffers: it uploads the palettes into frame scratch, keeps one
// GpuOnly posed buffer per instance per frame in flight (a frame still
// reading slot N's buffer must not race the next frame's dispatch into it),
// resolves each instance's rest and influence buffers, and drives the
// pre-skin dispatch. Everything downstream then draws the posed buffer as
// static geometry.
//
// Holds the backend pass by value, which is why this header is on the
// render-isolation allowlist: the recording lives in graphics/vulkan; what
// stays here is the policy -- which instances, which palettes, buffer
// lifecycle.
//
// Instances past the pass's per-frame dispatch budget keep a null posed
// buffer and their items fall back to rest geometry -- visible degradation,
// never a dropped draw.
//=============================================================================
class SkinnedPoseRenderFeature : public IRenderFeature
{
public:
    SkinnedPoseRenderFeature(std::shared_ptr<SkinnedPoseFrameData> frameData,
                             const SkinnedMeshCache& skinnedMeshes);

    [[nodiscard]] RenderPhase GetPhase() const override
    {
        return RenderPhase::Offscreen;
    }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

private:
    // One instance's retained posed buffers, keyed by its entity. Slots
    // rebuild when the vertex count changes (a mesh swap on the same
    // entity); the buffer service's fence-anchored deletion makes direct
    // Destroy safe with frames in flight.
    struct PosedSlots
    {
        RenderEntityKey Key;
        std::uint32_t VertexCount = 0;
        std::array<BufferHandle, kMaxFramesInFlight> Buffers{};
        std::uint32_t LastSeenFrame = 0;
    };

    [[nodiscard]] BufferHandle AcquirePosedBuffer(const RenderEntityKey& key,
                                                  std::uint32_t vertexCount,
                                                  std::uint32_t frameInFlight);
    void PruneStale();

    std::shared_ptr<SkinnedPoseFrameData> FrameData;
    const SkinnedMeshCache* SkinnedMeshes = nullptr;
    GpuFrameScratch* Scratch = nullptr;
    const RenderInstrumentation* Instrumentation = nullptr;
    SkinnedPosePass Pass;
    std::vector<PosedSlots> Slots;
    std::vector<SkinnedPoseDispatch> DispatchScratch;
    std::uint32_t FrameCounter = 0;
};
