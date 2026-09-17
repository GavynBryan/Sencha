#pragma once

#include "render/ImGuiTargetPresenter.h"

#include <core/handle/Handle.h>
#include <graphics/RenderFeature.h>
#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/UiDrawPass.h>
#include <math/Vec.h>
#include <ui/UiSurface.h>

#include <imgui.h>

#include <vector>

class TextureCache;
class UiService;

//=============================================================================
// UiSurfaceTargetRenderFeature
//
// Draws authored surfaces the host has taken offscreen into render targets an
// ImGui panel can show: a document previewed at its own resolution inside a
// dock, an authored panel occupying part of an editor's layout.
//
// One feature, any number of bindings. Each binding is one surface and one
// target sized to the surface every frame, so the recording's projection and
// the target's viewport agree by construction; fitting the result into a
// panel is ImGui's job. The engine's own UiRenderFeature keeps drawing the
// window-destined surfaces and never sees these; the runtime publishes a
// recording to exactly one of the two.
//
// Editor-side on purpose. The engine exports the reusable half -- UiDrawPass
// over a plain FrameContext -- and the target store and the ImGui binding are
// editor concerns, the same split Shudei's material preview made.
//
// The target is cleared opaque with the host's backdrop. The UI pass writes
// premultiplied alpha and ImGui composites straight alpha, so a transparent
// target would be alpha-multiplied twice at every anti-aliased edge; a solid
// ground under the document is the honest answer, and a checker would be a
// draw inside the scope rather than a clear value.
//=============================================================================
using UiSurfaceTargetId = Handle<struct UiSurfaceTargetTag>;

class UiSurfaceTargetRenderFeature final : public IRenderFeature
{
public:
    // `textures` may be null: content images then do not draw, the answer the
    // runtime itself gives when asked to resolve one.
    UiSurfaceTargetRenderFeature(UiService& ui, TextureCache* textures);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

    // Draws `surface` into a target of its own from the next frame on. The
    // surface should be Offscreen-destined, or the window feature draws it
    // too. Linear RGB for the ground; alpha is always 1.
    [[nodiscard]] UiSurfaceTargetId Bind(UiSurfaceId surface, Vec3d clearLinear);
    void Unbind(UiSurfaceTargetId binding);
    void SetClearColor(UiSurfaceTargetId binding, Vec3d clearLinear);

    // Panel side: the texture to show, at the surface's size. 0 until the
    // binding has rendered once, and for a binding that is gone. A host may
    // Bind before this feature is set up and hold the id: the binding is a
    // declaration, and the target behind it appears on the first frame drawn.
    [[nodiscard]] ImTextureID Display(UiSurfaceTargetId binding);
    [[nodiscard]] std::size_t BindingCount() const;

private:
    struct Binding
    {
        UiSurfaceId Surface;
        RenderTargetId Target;
        Vec3d Clear{};
        std::uint32_t Generation = 1;
        bool Live = false;
        bool Rendered = false;
    };
    [[nodiscard]] Binding* Resolve(UiSurfaceTargetId binding);
    // Creates the binding's target if it has none yet. False when it could
    // not be created, which the caller treats as "nothing to draw this frame".
    [[nodiscard]] bool EnsureTarget(Binding& binding);

    UiService& Ui;
    TextureCache* Textures = nullptr;
    RendererServices Services{};
    RenderTargetStore Targets;
    ImGuiTargetPresenter Presenter;
    UiDrawPass Pass;
    std::vector<Binding> Bindings;
    bool Ready = false;
};
