#pragma once

#include "render/DisplayedTargets.h"

#include <core/handle/Handle.h>
#include <graphics/RenderFeature.h>
#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/UiDrawPass.h>
#include <math/Vec.h>
#include <render/ui/UiDrawFrame.h>
#include <ui/UiSurface.h>


#include <optional>
#include <vector>

class TextureCache;
class UiService;

//=============================================================================
// UiSurfaceTargetRenderFeature
//
// Draws authored surfaces the host has taken offscreen into render targets a
// host image shows: a document previewed at its own resolution inside an
// editor's content.
//
// One feature, any number of bindings. Each binding is one surface and one
// target sized to the surface every frame, so the recording's projection and
// the target's viewport agree by construction; fitting the picture into the
// content is the document showing it. The engine's own UiRenderFeature keeps
// drawing the window-destined surfaces and never sees these; the runtime
// publishes a recording to exactly one of the two.
//
// The target is cleared opaque with the host's backdrop: a solid ground under
// the document composites the same whatever shows it, and a checker would be
// a draw inside the scope rather than a clear value.
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

    // UI side, each frame it is shown: renders the binding at the surface's
    // size from the next frame on, into the host image named. False while
    // there is nothing to show: not set up, or the binding gone. A host may
    // Bind before this feature is set up and hold the id: the binding is a
    // declaration, and the target behind it appears once it is shown.
    bool Show(UiSurfaceTargetId binding, UiHostImageId image);
    [[nodiscard]] std::size_t BindingCount() const;

private:
    struct Binding
    {
        UiSurfaceId Surface;
        RenderTargetId Target;
        Vec3d Clear{};
        std::optional<UiHostImageId> Image;
        std::uint32_t Generation = 1;
        bool Live = false;
    };
    [[nodiscard]] Binding* Resolve(UiSurfaceTargetId binding);
    void EnsureTarget(Binding& binding);

    UiService& Ui;
    TextureCache* Textures = nullptr;
    RendererServices Services{};
    DisplayedTargets Targets;
    UiDrawPass Pass;
    std::vector<Binding> Bindings;
    bool Ready = false;
};
