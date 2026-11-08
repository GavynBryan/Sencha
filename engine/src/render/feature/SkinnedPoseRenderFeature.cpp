#include <render/feature/SkinnedPoseRenderFeature.h>

#include <graphics/GpuFrameScratch.h>
#include <graphics/RenderFrameHealth.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace
{
// Frames a slot may go unseen before its buffers free. Anything beyond the
// frames-in-flight window is safe; the slack keeps a briefly-culled
// character from churning buffer creation every time it clips the frustum.
constexpr std::uint32_t kPruneAfterFrames = 60;

// Storage-buffer descriptor offsets must honor the device minimum, and 256 is
// the ceiling the Vulkan spec puts on minStorageBufferOffsetAlignment.
constexpr std::uint64_t kPaletteAlignment = 256;
} // namespace

SkinnedPoseRenderFeature::SkinnedPoseRenderFeature(
    std::shared_ptr<SkinnedPoseFrameData> frameData,
    const SkinnedMeshCache& skinnedMeshes,
    SkinnedPoseBudget budget)
    : FrameData(std::move(frameData))
    , SkinnedMeshes(&skinnedMeshes)
    , Budget(budget)
{
}

bool SkinnedPoseRenderFeature::Setup(const RenderFeatureServices& services)
{
    Scratch = services.Scratch;
    Instrumentation = services.Instrumentation;
    Health = services.Backend->Health;
    Log = services.Logging != nullptr
        ? &services.Logging->GetLogger<SkinnedPoseRenderFeature>() : nullptr;
    Pass.Setup(*services.Backend, Budget.Dispatches);
    return true;
}

SkinnedPoseRenderFeature::PosedSlots& SkinnedPoseRenderFeature::SlotFor(const RenderEntityKey& key)
{
    const auto it = std::lower_bound(Slots.begin(), Slots.end(), key,
                                     [](const PosedSlots& slot, const RenderEntityKey& wanted) { return slot.Key < wanted; });
    if (it != Slots.end() && it->Key == key)
        return *it;
    return *Slots.insert(it, PosedSlots{ .Key = key });
}

void SkinnedPoseRenderFeature::ReleaseBuffers(PosedSlots& slots)
{
    for (BufferHandle& buffer : slots.Buffers)
    {
        Pass.DestroyPosedBuffer(buffer);
        buffer = {};
    }
    slots.Versions = {};
}

void SkinnedPoseRenderFeature::PruneStale()
{
    std::erase_if(Slots, [this](PosedSlots& slots) {
        if (FrameCounter - slots.LastSeenFrame <= kPruneAfterFrames)
            return false;
        ReleaseBuffers(slots);
        return true;
    });
}

void SkinnedPoseRenderFeature::OnDraw(const RenderFrame& frame)
{
    ++FrameCounter;
    PruneStale();

    SkinnedPoseFrameData& data = *FrameData;
    data.PosedBuffers.assign(data.Instances.size(), BufferHandle{});
    Attempted.assign(data.Instances.size(), 0);

    SkinnedPoseOutcome outcome;
    if (!data.Instances.empty() && Scratch != nullptr)
        RecordPoses(frame, outcome);
    ResolveUnposed(frame, outcome);
    PublishOutcome(outcome);
}

void SkinnedPoseRenderFeature::RecordPoses(const RenderFrame& frame, SkinnedPoseOutcome& outcome)
{
    SkinnedPoseFrameData& data = *FrameData;

    Candidates.clear();
    for (const SkinnedPoseInstance& instance : data.Instances)
    {
        PosedSlots& slot = SlotFor(instance.Key);
        slot.LastSeenFrame = FrameCounter;
        Candidates.push_back(SkinnedPoseCandidate{
            .Key = instance.Key,
            .CameraDepth = instance.CameraDepth,
            .JointCount = instance.JointCount,
            .FramesWaited = FrameCounter - slot.LastDispatchFrame,
            .NeverPosed = !slot.Versions.HasPose(),
        });
    }

    const SkinnedPoseBudget budget{
        Budget.Dispatches,
        std::min(Budget.PaletteBytes, Scratch->AvailableBytes(ScratchTag::SkinningPalettes, kPaletteAlignment)),
    };
    SelectSkinnedPoses(Candidates, budget, Chosen);
    if (Chosen.empty())
        return;

    std::uint64_t paletteBytes = 0;
    for (const std::uint32_t index : Chosen)
    {
        Attempted[index] = 1;
        paletteBytes += SkinnedPalettePackedBytes(data.Instances[index].JointCount);
    }
    const GpuFrameScratch::Allocation palettes =
        Scratch->Allocate(paletteBytes, kPaletteAlignment, ScratchTag::SkinningPalettes);
    if (!palettes.IsValid())
    {
        outcome.PoseFailures += static_cast<std::uint32_t>(Chosen.size());
        return;
    }

    const std::uint32_t versionCount = Scratch->GetFramesInFlight();
    Dispatches.clear();
    DispatchTargets.clear();
    std::uint64_t paletteOffset = 0;
    for (const std::uint32_t index : Chosen)
    {
        const SkinnedPoseInstance& instance = data.Instances[index];
        const std::uint64_t instanceOffset = paletteOffset;
        paletteOffset += SkinnedPalettePackedBytes(instance.JointCount);
        CopyPalettesColumnMajor(
            std::span<const Mat4>(data.Palettes).subspan(instance.PaletteOffset, instance.JointCount),
            std::span<float>(reinterpret_cast<float*>(static_cast<std::byte*>(palettes.Mapped) + instanceOffset),
                             instance.JointCount * kPaletteMatrixFloats));

        const GpuStaticMesh* mesh = SkinnedMeshes->Get(instance.Mesh);
        const BufferHandle influences = SkinnedMeshes->GetInfluences(instance.Mesh);
        if (mesh == nullptr || !influences.IsValid() || mesh->VertexCount == 0)
        {
            ++outcome.PoseFailures;
            continue;
        }

        PosedSlots& slot = SlotFor(instance.Key);
        if (slot.VertexCount != mesh->VertexCount)
        {
            ReleaseBuffers(slot);
            slot.VertexCount = mesh->VertexCount;
        }
        const std::optional<std::uint32_t> target = slot.Versions.FindWritable(frame.Retirement, versionCount);
        if (!target.has_value())
        {
            ++outcome.PoseFailures;
            continue;
        }
        BufferHandle& buffer = slot.Buffers[*target];
        if (!buffer.IsValid())
            buffer = Pass.CreatePosedBuffer(mesh->VertexCount);
        if (!buffer.IsValid())
        {
            ++outcome.PoseFailures;
            continue;
        }

        Dispatches.push_back(SkinnedPoseDispatch{
            .RestVertices = mesh->VertexBuffer,
            .Influences = influences,
            .PosedVertices = buffer,
            .VertexCount = mesh->VertexCount,
            .PaletteBuffer = palettes.Buffer,
            .PaletteOffset = palettes.Offset + instanceOffset,
            .PaletteBytes = instance.JointCount * sizeof(Mat4),
        });
        DispatchTargets.push_back(DispatchTarget{ index, *target });
    }

    Recorded.assign(Dispatches.size(), 0);
    BeginGpuScope(frame, GpuScope::SkinPose);
    Pass.Record(*frame.Backend, Dispatches, Recorded);
    EndGpuScope(frame, GpuScope::SkinPose);

    for (std::size_t dispatch = 0; dispatch < Dispatches.size(); ++dispatch)
    {
        if (Recorded[dispatch] == 0)
        {
            ++outcome.PoseFailures;
            continue;
        }
        const DispatchTarget target = DispatchTargets[dispatch];
        PosedSlots& slot = SlotFor(data.Instances[target.Instance].Key);
        if (slot.Versions.HasPose())
            ++outcome.Refreshed;
        else
            ++outcome.FirstPose;
        slot.Versions.MarkWritten(target.Version, frame.Retirement);
        slot.LastDispatchFrame = FrameCounter;
        data.PosedBuffers[target.Instance] = slot.Buffers[target.Version];
    }
}

void SkinnedPoseRenderFeature::ResolveUnposed(const RenderFrame& frame, SkinnedPoseOutcome& outcome)
{
    SkinnedPoseFrameData& data = *FrameData;
    for (std::size_t index = 0; index < data.Instances.size(); ++index)
    {
        if (data.PosedBuffers[index].IsValid())
            continue;
        PosedSlots& slot = SlotFor(data.Instances[index].Key);
        if (const std::optional<std::uint32_t> latest = slot.Versions.ReadLatest(frame.Retirement))
        {
            data.PosedBuffers[index] = slot.Buffers[*latest];
            ++outcome.Held;
        }
        else if (Attempted[index] != 0)
        {
            ++outcome.BindFallback;
        }
        else
        {
            ++outcome.AwaitingFirstPose;
        }
    }
}

void SkinnedPoseRenderFeature::PublishOutcome(const SkinnedPoseOutcome& outcome)
{
    std::uint32_t posedBuffers = 0;
    std::uint64_t posedBytes = 0;
    for (const PosedSlots& slots : Slots)
    {
        for (const BufferHandle buffer : slots.Buffers)
        {
            if (!buffer.IsValid())
                continue;
            ++posedBuffers;
            posedBytes += SkinnedPosePass::PosedBufferBytes(slots.VertexCount);
        }
    }

    if (Health != nullptr)
    {
        Health->SkinnedFirstPose += outcome.FirstPose;
        Health->SkinnedRefreshed += outcome.Refreshed;
        Health->SkinnedHeld += outcome.Held;
        Health->SkinnedAwaitingFirstPose += outcome.AwaitingFirstPose;
        Health->SkinnedBindFallback += outcome.BindFallback;
        Health->SkinnedPoseFailures += outcome.PoseFailures;
        Health->PosedBuffers += posedBuffers;
        Health->PosedBufferBytes += posedBytes;
    }
    if (Instrumentation != nullptr && Instrumentation->Stats != nullptr)
    {
        RenderStats& out = *Instrumentation->Stats;
        out.SkinnedFirstPose = outcome.FirstPose;
        out.SkinnedRefreshed = outcome.Refreshed;
        out.SkinnedHeld = outcome.Held;
        out.SkinnedAwaitingFirstPose = outcome.AwaitingFirstPose;
        out.SkinnedBindFallback = outcome.BindFallback;
        out.SkinnedPoseFailures = outcome.PoseFailures;
        out.PosedBuffers = posedBuffers;
        out.PosedBufferBytes = posedBytes;
    }

    if (outcome.PoseFailures > 0 && PoseFailureLog.Record() && Log != nullptr)
    {
        Log->Error("{} chosen skinned instances could not be posed this frame; they hold their last pose "
                   "or draw at rest",
                   outcome.PoseFailures);
    }
    if (const auto ended = PoseFailureLog.EndPeriod(); ended.has_value() && Log != nullptr)
        Log->Warn("Skinned pose dispatches are succeeding again after {} frames of failures", ended->Periods);

    if (outcome.Held > 0 && OverBudgetLog.Record() && Log != nullptr)
    {
        Log->Warn("{} skinned instances are past the {} poses per frame budget and hold their last pose",
                  outcome.Held, Budget.Dispatches);
    }
    if (const auto ended = OverBudgetLog.EndPeriod(); ended.has_value() && Log != nullptr)
        Log->Info("Skinned instances are within the pose budget again after {} frames", ended->Periods);
}

void SkinnedPoseRenderFeature::Teardown()
{
    for (PosedSlots& slots : Slots)
        ReleaseBuffers(slots);
    Slots.clear();
    Pass.Teardown();
}
