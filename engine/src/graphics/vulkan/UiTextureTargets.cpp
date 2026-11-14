#include <graphics/vulkan/UiTextureTargets.h>

#include <core/logging/Logger.h>
#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

namespace
{
// The swapchain's format, as for every UI target: the UI shaders write linear
// colour and rely on the attachment encoding it, and a material samples it back
// through an sRGB view, so the round trip is exact.
constexpr VkFormat kTextureColorFormat = VK_FORMAT_B8G8R8A8_SRGB;
} // namespace

bool UiTextureTargets::Setup(const RendererServices& services, TextureCache* textures, Logger* log)
{
    Services = services;
    Log = log;
    Store.Setup(Services);
    return Pass.Setup(Services, textures);
}

void UiTextureTargets::Teardown(TargetTextureTable& targets)
{
    for (const Bound& bound : Bindings)
        targets.Withdraw(bound.Texture);
    Bindings.clear();
    Pass.Teardown();
    Store.Teardown();
}

UiTextureTargets::Bound& UiTextureTargets::BindingFor(TargetTextureId texture)
{
    for (Bound& bound : Bindings)
        if (bound.Texture == texture)
            return bound;

    RenderTargetDesc desc{};
    desc.ColorFormat = kTextureColorFormat;
    // The device's depth format, stencil-bearing where it offers one, so a
    // document clips through the stencil exactly as it does on the window.
    desc.DepthFormat = Services.DepthFormat;
    desc.Read = RenderTargetRead::Bindless;
    desc.Sampler = Services.Samplers != nullptr ? Services.Samplers->GetLinearClamp() : VK_NULL_HANDLE;
    desc.Lifetime = RenderTargetLifetime::WithinFrame;
    desc.DebugName = "ui_texture";
    Bound& bound = Bindings.emplace_back();
    bound.Texture = texture;
    bound.Image = Store.Create(desc);
    return bound;
}

void UiTextureTargets::Draw(const FrameContext& context, const RenderTargetView& target, const UiDrawFrame& ui,
                            RenderStats* stats)
{
    RenderTargetSession session(context.Cmd, target.ColorImage, *target.ColorAccess, target.DepthImage,
                                Services.DepthFormat);
    const RenderScope rendering(context, UiTargetScope(target, kTextureColorFormat, Services,
                                                       VkClearColorValue{ { 0.f, 0.f, 0.f, 0.f } }));
    // An empty recording is a document showing nothing: cleared, not withdrawn.
    if (ui.IsEmpty())
        return;
    Pass.Draw(rendering.Context(), ui);
    if (stats == nullptr)
        return;
    const UiDrawPass::DrawStats& drawn = Pass.GetLastDrawStats();
    stats->UiDrawCalls += drawn.DrawCalls;
    stats->UiTriangles += drawn.Triangles;
    stats->UiTextureUploads += drawn.TextureUploads;
    stats->UiTextureUploadBytes += drawn.TextureUploadBytes;
}

void UiTextureTargets::Record(const RenderFrame& frame, const std::vector<UiTextureFrame>& surfaces,
                              TargetTextureTable& targets)
{
    if (frame.Backend == nullptr)
        return;
    const FrameContext& context = *frame.Backend;
    RenderStats* stats = frame.Instrumentation != nullptr ? frame.Instrumentation->Stats : nullptr;

    for (Bound& bound : Bindings)
        bound.Seen = false;
    Store.BeginFrame(context.FrameInFlightIndex);

    bool scoped = false;
    for (const UiTextureFrame& entry : surfaces)
    {
        Bound& bound = BindingFor(targets.Intern(entry.Texture));
        if (bound.Seen)
        {
            // Two surfaces drawing one texture would overwrite each other; the
            // first published keeps it.
            if (!WarnedShared && Log != nullptr)
                Log->Warn("Two UI surfaces draw into the target texture '{}'; only the first is shown",
                          entry.Texture);
            WarnedShared = true;
            continue;
        }
        bound.Seen = true;
        bound.Quiet.NoteUsed();

        Store.SetExtent(bound.Image, VkExtent2D{ entry.Frame.Surface.Width, entry.Frame.Surface.Height });
        const std::optional<RenderTargetView> target = Store.Acquire(bound.Image);
        if (!target)
        {
            targets.Withdraw(bound.Texture);
            bound.Holds = false;
            continue;
        }

        // A freshly built image has never been written: whatever Drawn says, it
        // holds nothing.
        const bool written = target->ColorAccess->Layout != VK_IMAGE_LAYOUT_UNDEFINED;
        if (!written || !bound.Holds || !SameUiRecording(bound.Drawn, entry.Frame))
        {
            if (!scoped)
                BeginGpuScope(frame, GpuScope::UiTextures);
            scoped = true;
            Draw(context, *target, entry.Frame, stats);
            bound.Drawn = entry.Frame;
            bound.Holds = true;
        }
        // Every frame, before the forward pass records: the index changes when
        // the image is rebuilt, and the table is what materials resolve through.
        targets.Publish(bound.Texture, target->BindlessIndex);
    }
    if (scoped)
        EndGpuScope(frame, GpuScope::UiTextures);

    for (Bound& bound : Bindings)
    {
        if (bound.Seen)
            continue;
        targets.Withdraw(bound.Texture);
        if (bound.Quiet.NoteQuiet())
        {
            Store.Evict(bound.Image);
            bound.Drawn = {};
            bound.Holds = false;
        }
    }
}
