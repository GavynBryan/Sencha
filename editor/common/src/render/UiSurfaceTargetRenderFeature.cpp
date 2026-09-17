#include "render/UiSurfaceTargetRenderFeature.h"

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <render/ui/UiDrawFrame.h>
#include <ui/UiService.h>

namespace
{
    // The swapchain's format, on purpose. The UI shaders write linear colour
    // and rely on the attachment encoding it; sampled back by ImGui and
    // written into the sRGB swapchain, the round trip is exact.
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
    Targets.Setup(Services);
    // Linear, because the panel fits a full-resolution surface into whatever
    // space it has; nearest would shimmer at every non-integer ratio.
    if (Services.Samplers != nullptr)
        Presenter.Setup(Services.Samplers->GetLinearClamp());
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
        // new target for it. Rendered goes with the target, so Display stops
        // handing out a texture that no longer exists.
        Presenter.Release(binding.Target);
        Targets.Destroy(binding.Target);
        binding.Target = {};
        binding.Rendered = false;
    }
    Targets.Teardown();
    Presenter.Teardown();
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
    binding.Rendered = false;
    binding.Live = true;
    binding.Target = {};

    return UiSurfaceTargetId{ static_cast<std::uint32_t>(index + 1), binding.Generation };
}

bool UiSurfaceTargetRenderFeature::EnsureTarget(Binding& binding)
{
    // The target is made on the first frame the binding is drawn, not when it
    // is declared: its depth format is the device's, and a composition root
    // binds while it is wiring panels -- before any feature has been set up.
    if (binding.Target.IsValid())
        return true;

    RenderTargetDesc desc{};
    desc.ColorFormat = kTargetColorFormat;
    // The device's depth format, stencil-bearing where it offers one: the UI
    // pass clips rounded boundaries through the stencil, and a target without
    // it would preview a document clipping differently from the shipped game.
    desc.DepthFormat = Services.DepthFormat;
    desc.Read = RenderTargetRead::Sampled;
    desc.DebugName = "ui_surface_target";
    binding.Target = Targets.Create(desc);
    return binding.Target.IsValid();
}

void UiSurfaceTargetRenderFeature::Unbind(UiSurfaceTargetId id)
{
    Binding* binding = Resolve(id);
    if (binding == nullptr)
        return;
    Presenter.Release(binding->Target);
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

ImTextureID UiSurfaceTargetRenderFeature::Display(UiSurfaceTargetId id)
{
    Binding* binding = Resolve(id);
    if (binding == nullptr || !binding->Rendered)
        return 0;
    return Presenter.Present(Targets, binding->Target);
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
    Presenter.BeginFrame(frame.Retirement);

    for (Binding& binding : Bindings)
    {
        if (!binding.Live || !EnsureTarget(binding))
            continue;

        // The target follows the surface, so the recording's projection and
        // the target's viewport agree. A surface with no size yet draws nothing.
        const RenderExtent size = Ui.GetSurfaceSize(binding.Surface);
        if (size.Width == 0 || size.Height == 0)
            continue;
        Targets.SetExtent(binding.Target, VkExtent2D{ size.Width, size.Height });
        const std::optional<RenderTargetView> target = Targets.Acquire(binding.Target);
        if (!target)
            continue;

        // Brackets the recording and commits the layout the store remembers.
        // The depth format decides the aspects the barrier names; a combined
        // image transitioned as depth alone leaves its stencil half behind.
        RenderTargetSession session(frame.Cmd, target->ColorImage, target->ColorLayout,
                                    target->DepthImage, Services.DepthFormat);

        RenderScopeDesc scope{};
        scope.Area.offset = { 0, 0 };
        scope.Area.extent = target->Extent;
        scope.Color.View = target->ColorView;
        scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        scope.Color.Clear.color = { { binding.Clear.X, binding.Clear.Y, binding.Clear.Z, 1.0f } };
        scope.ColorFormat = kTargetColorFormat;
        scope.Depth.View = target->DepthView;
        scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        scope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        scope.Depth.Clear.depthStencil = { 1.0f, 0 };
        scope.DepthFormat = Services.DepthFormat;
        if (Services.StencilFormat != VK_FORMAT_UNDEFINED)
        {
            // Same image, its other aspect: cleared, discarded, and what the
            // pass clips through -- exactly as the swapchain frame binds it.
            scope.Stencil.View = target->DepthView;
            scope.Stencil.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            scope.Stencil.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            scope.Stencil.Clear.depthStencil = { 1.0f, 0 };
            scope.StencilFormat = Services.StencilFormat;
        }
        scope.Phase = RenderPhase::Offscreen;

        {
            const RenderScope rendering(frame, scope);
            // A surface that drew nothing this frame still gets its ground.
            if (const UiDrawFrame* ui = Ui.OffscreenFrame(binding.Surface))
                Pass.Draw(rendering.Context(), *ui);
        }
        binding.Rendered = true;
    }
}
