#include "render/UiSurfaceTargetRenderFeature.h"

#include "render/TargetImagePublish.h"

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <render/ui/UiDrawFrame.h>
#include <ui/UiService.h>

namespace
{
    // The swapchain's format, on purpose. The UI shaders write linear colour
    // and rely on the attachment encoding it; sampled back through a host
    // image and written into the sRGB swapchain, the round trip is exact.
    constexpr VkFormat kTargetColorFormat = VK_FORMAT_B8G8R8A8_SRGB;
}

UiSurfaceTargetRenderFeature::UiSurfaceTargetRenderFeature(UiService& ui, TextureCache* textures)
    : Ui(ui)
    , Textures(textures)
{
}

bool UiSurfaceTargetRenderFeature::Setup(const RenderFeatureServices& featureServices)
{
    if (featureServices.Backend == nullptr)
        return false;
    Services = *featureServices.Backend;
    // Linear, because the panel fits a full-resolution surface into whatever
    // space it has; nearest would shimmer at every non-integer ratio.
    Targets.Setup(Services);
    Ready = Pass.Setup(Services, Textures);
    return Ready;
}

void UiSurfaceTargetRenderFeature::Teardown()
{
    Pass.Teardown();
    for (Binding& binding : Bindings)
    {
        // A binding is the host's declaration and outlives the device state it
        // names: the target goes, the binding stays, and the next Setup makes a
        // new target for it.
        binding.Target = {};
    }
    Targets.Teardown();
    Ready = false;
}

UiSurfaceTargetRenderFeature::Binding* UiSurfaceTargetRenderFeature::Resolve(UiSurfaceTargetId id)
{
    if (!id.IsValid() || id.Index > Bindings.size())
        return nullptr;
    Binding& binding = Bindings[id.Index - 1];
    return binding.Live && binding.Generation == id.Generation ? &binding : nullptr;
}

UiSurfaceTargetId UiSurfaceTargetRenderFeature::Bind(UiSurfaceId surface, Vec3d clearLinear)
{
    std::size_t index = Bindings.size();
    for (std::size_t i = 0; i < Bindings.size(); ++i)
    {
        if (!Bindings[i].Live)
        {
            index = i;
            break;
        }
    }
    if (index == Bindings.size())
        Bindings.emplace_back();

    Binding& binding = Bindings[index];
    binding.Surface = surface;
    binding.Clear = clearLinear;
    binding.Live = true;
    binding.Target = {};

    return UiSurfaceTargetId{ static_cast<std::uint32_t>(index + 1), binding.Generation };
}

void UiSurfaceTargetRenderFeature::EnsureTarget(Binding& binding)
{
    // Made on first display, not when declared: its depth format is the
    // device's, and a composition root binds before any feature is set up.
    if (binding.Target.IsValid())
        return;

    RenderTargetDesc desc{};
    desc.ColorFormat = kTargetColorFormat;
    // The device's depth format, stencil-bearing where it offers one: the UI
    // pass clips rounded boundaries through the stencil, and a target without
    // it would preview a document clipping differently from the shipped game.
    desc.DepthFormat = Services.DepthFormat;
    desc.Read = RenderTargetRead::Bindless;
    desc.Sampler = Services.Samplers != nullptr ? Services.Samplers->GetLinearClamp() : VK_NULL_HANDLE;
    desc.DebugName = "ui_surface_target";
    binding.Target = Targets.Create(desc);
}

void UiSurfaceTargetRenderFeature::Unbind(UiSurfaceTargetId id)
{
    Binding* binding = Resolve(id);
    if (binding == nullptr)
        return;
    Targets.Destroy(binding->Target);
    binding->Target = {};
    binding->Live = false;
    ++binding->Generation;
}

void UiSurfaceTargetRenderFeature::SetClearColor(UiSurfaceTargetId id, Vec3d clearLinear)
{
    if (Binding* binding = Resolve(id))
        binding->Clear = clearLinear;
}

bool UiSurfaceTargetRenderFeature::Show(UiSurfaceTargetId id, UiHostImageId image)
{
    Binding* binding = Resolve(id);
    if (binding == nullptr || !Ready)
        return false;
    EnsureTarget(*binding);
    binding->Image = image;
    // The target follows the surface, so the recording's projection and the
    // target's viewport agree.
    const RenderExtent size = Ui.GetSurfaceSize(binding->Surface);
    Targets.Show(binding->Target, VkExtent2D{ size.Width, size.Height });
    return true;
}

std::size_t UiSurfaceTargetRenderFeature::BindingCount() const
{
    std::size_t live = 0;
    for (const Binding& binding : Bindings)
        live += binding.Live ? 1 : 0;
    return live;
}

void UiSurfaceTargetRenderFeature::OnDraw(const RenderFrame& renderFrame)
{
    if (!Ready || renderFrame.Backend == nullptr)
        return;
    const FrameContext& frame = *renderFrame.Backend;
    Targets.BeginFrame(frame.FrameInFlightIndex);

    for (Binding& binding : Bindings)
    {
        if (!binding.Live)
            continue;
        const std::optional<RenderTargetView> target = Targets.Acquire(binding.Target);
        if (!target)
        {
            if (binding.Image.has_value() && Services.UiTextures != nullptr)
                Services.UiTextures->WithdrawHostImage(*binding.Image);
            continue;
        }

        // Brackets the recording and commits the layout the store remembers.
        // The depth format decides the aspects the barrier names; a combined
        // image transitioned as depth alone leaves its stencil half behind.
        RenderTargetSession session(frame.Cmd, target->ColorImage, target->ColorLayout,
                                    target->DepthImage, Services.DepthFormat);

        const RenderScopeDesc scope = UiTargetScope(
            *target, kTargetColorFormat, Services,
            VkClearColorValue{ { binding.Clear.X, binding.Clear.Y, binding.Clear.Z, 1.0f } });

        {
            const RenderScope rendering(frame, scope);
            // A surface that drew nothing this frame still gets its ground.
            if (const UiDrawFrame* ui = Ui.OffscreenFrame(binding.Surface))
                Pass.Draw(rendering.Context(), *ui);
        }
        session.End();
        if (binding.Image.has_value() && Services.UiTextures != nullptr)
            PublishTargetImage(*Services.UiTextures, *binding.Image, target->ColorLayout, target->BindlessIndex);
    }
}
