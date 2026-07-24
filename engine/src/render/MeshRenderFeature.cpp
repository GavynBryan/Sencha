#include <render/MeshRenderFeature.h>

#include <profiling/CpuScopeTimings.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <graphics/vulkan/GpuTimestampPool.h>
#include <graphics/vulkan/VulkanDebugLabels.h>
#endif

MeshRenderFeature::MeshRenderFeature(RenderQueue& queue,
                                     StaticMeshCache& meshes,
                                     MaterialCache& materials,
                                     const CameraRenderData& camera,
                                     const RenderLightSet& lights,
                                     std::shared_ptr<LightBindings> bindings)
    : Queue(&queue)
    , Meshes(&meshes)
    , Materials(&materials)
    , Camera(&camera)
    , Lights(&lights)
    , Bindings(std::move(bindings))
{
}

bool MeshRenderFeature::Setup(const RendererServices& services)
{
    Instrumentation = services.Instrumentation;
    Pass.Setup(services, *Bindings);
    // The pass degrades to inert when the lighting bindings are unusable,
    // which is a deliberate policy: the frame still presents. That is not a
    // setup failure.
    return true;
}

void MeshRenderFeature::OnDraw(const FrameContext& frame)
{
    if (Queue == nullptr || Lights == nullptr) return;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    GpuTimestampPool* gpuScopes = Instrumentation != nullptr
        ? Instrumentation->GpuTimestamps
        : nullptr;
    if (gpuScopes != nullptr)
    {
        VulkanDebugLabels::BeginLabel(frame.Cmd, ToString(GpuScope::ForwardOpaque));
        gpuScopes->BeginScope(frame.Cmd, GpuScope::ForwardOpaque);
    }
#endif
    {
        CpuScopeTimer timer(
            Instrumentation != nullptr ? Instrumentation->CpuScopes : nullptr,
            CpuScope::ForwardRecord);
        Pass.Draw(frame, *Camera, *Lights, *Queue, *Meshes, *Materials);
    }
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        gpuScopes->EndScope(frame.Cmd, GpuScope::ForwardOpaque);
        VulkanDebugLabels::EndLabel(frame.Cmd);
    }
#endif

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
