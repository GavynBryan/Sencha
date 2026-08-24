#include <render/SkinnedPoseRenderFeature.h>

#include <graphics/vulkan/VulkanFrameScratch.h>
#include <profiling/RenderInstrumentation.h>

#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <graphics/vulkan/GpuTimestampPool.h>
#include <graphics/vulkan/VulkanDebugLabels.h>
#endif

#include <cstdint>
#include <cstring>

namespace
{
// Frames a slot may go unseen before its buffers free. Anything beyond the
// frames-in-flight window is safe; the slack keeps a briefly-culled
// character from churning buffer creation every time it clips the frustum.
constexpr std::uint32_t kPruneAfterFrames = 60;

// Storage-buffer descriptor offsets must honor the device minimum; 256 is
// the largest minStorageBufferOffsetAlignment any real device reports.
constexpr std::uint64_t kPaletteAlignment = 256;
} // namespace

SkinnedPoseRenderFeature::SkinnedPoseRenderFeature(
    std::shared_ptr<SkinnedPoseFrameData> frameData,
    const SkinnedMeshCache& skinnedMeshes)
    : FrameData(std::move(frameData))
    , SkinnedMeshes(&skinnedMeshes)
{
}

bool SkinnedPoseRenderFeature::Setup(const RendererServices& services)
{
    Scratch = services.Scratch;
    Instrumentation = services.Instrumentation;
    Pass.Setup(services);
    return true;
}

BufferHandle SkinnedPoseRenderFeature::AcquirePosedBuffer(
    const RenderEntityKey& key, std::uint32_t vertexCount,
    std::uint32_t frameInFlight)
{
    PosedSlots* slots = nullptr;
    for (PosedSlots& candidate : Slots)
    {
        if (candidate.Key == key)
        {
            slots = &candidate;
            break;
        }
    }
    if (slots == nullptr)
    {
        Slots.push_back(PosedSlots{ .Key = key });
        slots = &Slots.back();
    }

    // A vertex-count change means a different mesh behind the same entity;
    // every slot rebuilds (the deletion queue is fence-anchored, so frames
    // still reading the old buffers finish before they free).
    if (slots->VertexCount != vertexCount)
    {
        for (BufferHandle& buffer : slots->Buffers)
        {
            Pass.DestroyPosedBuffer(buffer);
            buffer = {};
        }
        slots->VertexCount = vertexCount;
    }
    slots->LastSeenFrame = FrameCounter;

    BufferHandle& buffer = slots->Buffers[frameInFlight];
    if (!buffer.IsValid())
    {
        buffer = Pass.CreatePosedBuffer(vertexCount);
    }
    return buffer;
}

void SkinnedPoseRenderFeature::PruneStale()
{
    for (auto it = Slots.begin(); it != Slots.end();)
    {
        if (FrameCounter - it->LastSeenFrame > kPruneAfterFrames)
        {
            for (BufferHandle& buffer : it->Buffers)
                Pass.DestroyPosedBuffer(buffer);
            it = Slots.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void SkinnedPoseRenderFeature::OnDraw(const FrameContext& frame)
{
    ++FrameCounter;
    PruneStale();

    SkinnedPoseFrameData& data = *FrameData;
    data.Ready = false;
    data.PosedBuffers.assign(data.Instances.size(), BufferHandle{});
    if (data.Instances.empty() || Scratch == nullptr)
        return;

    // The whole frame's palettes as one scratch allocation; each dispatch
    // binds its slice by offset. Scratch is host-visible and coherent, and
    // its slice rotation plus the frame fence order the write against the
    // dispatch that reads it.
    const std::uint64_t paletteBytes = data.Palettes.size() * sizeof(Mat4);
    const VulkanFrameScratch::Allocation palettes =
        Scratch->Allocate(paletteBytes, kPaletteAlignment);
    if (!palettes.IsValid())
        return; // items fall back to rest geometry, counted as not Ready
    std::memcpy(palettes.Mapped, data.Palettes.data(),
                static_cast<std::size_t>(paletteBytes));
    DispatchScratch.clear();
    for (std::size_t index = 0; index < data.Instances.size(); ++index)
    {
        const SkinnedPoseInstance& instance = data.Instances[index];
        const GpuStaticMesh* mesh = SkinnedMeshes->Get(instance.Mesh);
        const BufferHandle influences =
            SkinnedMeshes->GetInfluences(instance.Mesh);
        if (mesh == nullptr || !influences.IsValid()
            || mesh->VertexCount == 0)
        {
            continue;
        }

        const BufferHandle posed = AcquirePosedBuffer(
            instance.Key, mesh->VertexCount, frame.FrameInFlightIndex);
        if (!posed.IsValid())
            continue;

        data.PosedBuffers[index] = posed;
        DispatchScratch.push_back(SkinnedPoseDispatch{
            .RestVertices = mesh->VertexBuffer,
            .Influences = influences,
            .PosedVertices = posed,
            .VertexCount = mesh->VertexCount,
            .PaletteBuffer = palettes.Buffer,
            .PaletteOffset = palettes.Offset
                + instance.PaletteOffset * sizeof(Mat4),
            .PaletteBytes = instance.JointCount * sizeof(Mat4),
        });
    }

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    GpuTimestampPool* gpuScopes = Instrumentation != nullptr
        ? Instrumentation->GpuTimestamps
        : nullptr;
    if (gpuScopes != nullptr)
    {
        VulkanDebugLabels::BeginLabel(frame.Cmd, ToString(GpuScope::SkinPose));
        gpuScopes->BeginScope(frame.Cmd, GpuScope::SkinPose);
    }
#endif
    const std::uint32_t recorded = Pass.Record(frame, DispatchScratch);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        gpuScopes->EndScope(frame.Cmd, GpuScope::SkinPose);
        VulkanDebugLabels::EndLabel(frame.Cmd);
    }
#endif
    // A job the pass could not record (budget, allocation) left its slot's
    // entry stale-but-valid in PosedBuffers only if it dispatched; the pass
    // records in order and only drops the tail, so invalidate the tail.
    if (recorded < DispatchScratch.size())
    {
        std::size_t seen = 0;
        for (std::size_t index = 0; index < data.Instances.size(); ++index)
        {
            if (!data.PosedBuffers[index].IsValid())
                continue;
            if (seen >= recorded)
                data.PosedBuffers[index] = {};
            ++seen;
        }
    }
    data.Ready = recorded > 0;
}

void SkinnedPoseRenderFeature::Teardown()
{
    for (PosedSlots& slots : Slots)
    {
        for (BufferHandle& buffer : slots.Buffers)
        {
            Pass.DestroyPosedBuffer(buffer);
            buffer = {};
        }
    }
    Slots.clear();
    Pass.Teardown();
}
