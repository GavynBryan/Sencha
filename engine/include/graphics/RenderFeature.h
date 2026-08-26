#pragma once

#include <graphics/GpuBuffers.h>
#include <graphics/GpuFrameRetirement.h>
#include <graphics/GpuImages.h>
#include <graphics/RenderExtent.h>
#include <profiling/RenderInstrumentation.h>

#include <cstdint>

class GpuFrameScratch;
class LoggingProvider;
struct FrameContext;      // the active backend's per-frame recording state
struct RendererServices;  // the active backend's service bundle

//=============================================================================
// The render feature contract, backend-neutral.
//
// A feature is policy: it decides what to draw from render-domain state and
// drives passes that do the recording. This header is everything a feature
// needs to be declared -- phases, the services it is handed once, the frame
// it sees per draw -- and none of it names a graphics API.
//
// The backend types stay behind the two forward declarations. A feature that
// owns a recording pass hands `Backend` through to it and never dereferences
// it in policy code; a feature with no pass never includes a backend header
// at all. A second backend would define its own RendererServices and
// FrameContext behind the same declarations -- one backend per binary,
// selected at build time.
//=============================================================================

enum class RenderPhase : std::uint8_t
{
    // Features here own their render passes/targets (no swapchain pass is open) and
    // run before MainColor: e.g. the editor rendering viewports to offscreen textures.
    Offscreen = 0,
    MainColor = 1,
    // Reserved for: Shadow, Opaque, Transparent, UI, Post...
    Count
};

// What a feature receives once at Setup. The neutral members cover creating
// GPU resources, per-frame scratch, logging, and instrumentation; `Backend`
// is the bundle the recording passes take, and only a feature that owns a
// pass hands it through.
struct RenderFeatureServices
{
    LoggingProvider* Logging = nullptr;
    // Stable pointer for the renderer's life; members flip with
    // render.profile.mode, so cache the bundle and re-read members per frame.
    const RenderInstrumentation* Instrumentation = nullptr;
    GpuBuffers Buffers;
    GpuImages Images;
    GpuFrameScratch* Scratch = nullptr;
    const RendererServices* Backend = nullptr;
};

// The frame as a feature sees it. `Backend` carries the command stream for
// the passes the feature drives; policy code never dereferences it.
struct RenderFrame
{
    std::uint32_t FrameInFlightIndex = 0;
    RenderExtent TargetExtent{};
    RenderPhase Phase = RenderPhase::MainColor;
    // Fence-anchored frame clock. A feature releasing a GPU resource the
    // renderer may still be reading stamps it from here and frees it once this
    // reports it retired, rather than counting frames itself.
    GpuFrameRetirement Retirement;
    const RenderInstrumentation* Instrumentation = nullptr;
    const FrameContext* Backend = nullptr;
};

// Debug label plus GPU timestamp scope on the frame's command stream. No-ops
// when instrumentation is off or compiled out. Defined by the active backend.
void BeginGpuScope(const RenderFrame& frame, GpuScope scope);
void EndGpuScope(const RenderFrame& frame, GpuScope scope);

class IRenderFeature
{
public:
    virtual ~IRenderFeature() = default;

    // Which phase this feature runs in. One feature, one phase.
    [[nodiscard]] virtual RenderPhase GetPhase() const = 0;

    // Runs once, inside Renderer::AddFeature. Cache service pointers here.
    // Do any up-front GPU resource creation here too. Returning false means
    // the feature is not usable: AddFeature tears it down and refuses to
    // register it, rather than leaving an inert feature in a phase bucket.
    [[nodiscard]] virtual bool Setup(const RenderFeatureServices& services) = 0;

    // Per-frame record. For MainColor features the command stream is already
    // inside the swapchain rendering scope. Features in phases that open
    // their own passes own their own begin/end.
    virtual void OnDraw(const RenderFrame& frame) = 0;

    // Runs in ~Renderer before any backend service is torn down. Release
    // any GPU resources the feature still holds here.
    virtual void Teardown() {}
};
