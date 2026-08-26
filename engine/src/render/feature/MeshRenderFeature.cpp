#include <render/feature/MeshRenderFeature.h>

#include <profiling/CpuScopeTimings.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>


MeshRenderFeature::MeshRenderFeature(RenderQueue& queue,
                                     StaticMeshCache& meshes,
                                     MaterialCache& materials,
                                     const CameraRenderData& camera,
                                     const RenderLightSet& lights,
                                     std::shared_ptr<LightBindings> bindings,
                                     const SkinnedMeshCache* skinnedMeshes,
                                     std::shared_ptr<const SkinnedPoseFrameData> skinnedPoses,
                                     ProbeVolumeSet* probes)
    : Queue(&queue)
    , Meshes(&meshes)
    , SkinnedMeshes(skinnedMeshes)
    , Materials(&materials)
    , Camera(&camera)
    , Lights(&lights)
    , Bindings(std::move(bindings))
    , SkinnedPoses(std::move(skinnedPoses))
    , Probes(probes)
{
}

bool MeshRenderFeature::Setup(const RenderFeatureServices& services)
{
    Instrumentation = services.Instrumentation;
    Pass.Setup(*services.Backend, *Bindings);
    Pass.SetSkinnedPoses(SkinnedPoses.get());
    // The pass degrades to inert when the lighting bindings are unusable,
    // which is a deliberate policy: the frame still presents. That is not a
    // setup failure.
    return true;
}

void MeshRenderFeature::OnDraw(const RenderFrame& frame)
{
    // Ahead of the early-out: a frame that draws nothing still retires the
    // probe slots whose zones unloaded, and a stalled reclaim would deny the
    // zone streaming in behind them.
    if (Probes != nullptr)
        Probes->BeginFrame(frame.Retirement);

    if (Queue == nullptr || Lights == nullptr) return;
    BeginGpuScope(frame, GpuScope::ForwardOpaque);
    {
        CpuScopeTimer timer(
            Instrumentation != nullptr ? Instrumentation->CpuScopes : nullptr,
            CpuScope::ForwardRecord);
        Pass.Draw(*frame.Backend,
                  MeshForwardPass::DrawContext{ .Camera = *Camera,
                                                .Lights = *Lights,
                                                .Queue = *Queue,
                                                .Meshes = *Meshes,
                                                .Materials = *Materials,
                                                .SkinnedMeshes = SkinnedMeshes });
    }
    EndGpuScope(frame, GpuScope::ForwardOpaque);

    if (Instrumentation != nullptr && Instrumentation->Stats != nullptr)
    {
        const MeshForwardPass::DrawStats stats = Pass.GetLastDrawStats();
        RenderStats& out = *Instrumentation->Stats;
        out.VisibleObjects = stats.QueueItems;
        out.DrawCalls = stats.DrawCalls;
        out.SubmittedTriangles = stats.Triangles;
        out.PipelineSwitches = stats.PipelineSwitches;
        out.MaterialSwitches = stats.MaterialSwitches;
        out.InstancesDropped = stats.InstancesDropped;
        if (stats.Skipped)
            ++out.PassesSkipped;
    }
}

void MeshRenderFeature::Teardown()
{
    Pass.Teardown();
}
