#pragma once

#include <graphics/RenderExtent.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <memory>
#include <optional>
#include <string_view>

class AssetSystem;
class FontFaceCache;
class LoggingProvider;
class UiPackageCache;
class UiRuntime;

//=============================================================================
// UiService
//
// The authored UI layer as a game or a Sencha application sees it: surfaces,
// screens, and -- from Stage 4 -- presentation models and semantic actions.
//
// This is the whole public surface. It names no RmlUi type and never will;
// cmake/CheckUiIsolation.cmake is what keeps that true rather than aspirational.
// A host that wanted the DOM would be reaching past the boundary this layer
// exists to draw, and would couple itself to a layout engine that is an
// implementation choice.
//
// Owner-thread only. Documents, layout and the screen stack are all touched
// from the frame loop; nothing here is safe to call from a task thread.
//=============================================================================
class UiService
{
public:
    UiService(LoggingProvider& logging,
              AssetSystem& assets,
              UiPackageCache& packages,
              FontFaceCache& fonts);
    ~UiService();

    UiService(const UiService&) = delete;
    UiService& operator=(const UiService&) = delete;
    UiService(UiService&&) = delete;
    UiService& operator=(UiService&&) = delete;

    // False when the document engine could not be brought up. Every call below
    // is a safe no-op in that state rather than a crash: a game whose UI failed
    // to initialise should lose its menus, not its process.
    [[nodiscard]] bool IsReady() const;

    // -- surfaces ------------------------------------------------------------

    [[nodiscard]] UiSurfaceId CreateSurface(std::string_view name, RenderExtent size);
    void DestroySurface(UiSurfaceId surface);

    // Re-lays-out every screen on the surface. Called on window resize and on a
    // UI scale change -- a retained document re-flows where a baked atlas
    // cannot, which is why UI scale is live here and not latched at startup.
    void SetSurfaceSize(UiSurfaceId surface, RenderExtent size);
    [[nodiscard]] RenderExtent GetSurfaceSize(UiSurfaceId surface) const;

    // -- screens -------------------------------------------------------------

    // Opens the cooked package at `packagePath` (an "asset://..." virtual path)
    // on `surface`. The screen takes its own lease on the package and on every
    // resource the package's table names, and drops them when it closes.
    //
    // Invalid handle if the package is missing, is not a cooked .sui, or names
    // a resource that cannot be resolved. The failure is logged once with the
    // authoring detail; callers check the handle rather than an error code.
    [[nodiscard]] UiScreenHandle OpenScreen(UiSurfaceId surface, std::string_view packagePath);
    void CloseScreen(UiScreenHandle screen);
    [[nodiscard]] bool IsScreenOpen(UiScreenHandle screen) const;

    // -- frame ---------------------------------------------------------------

    // Applies pending presentation state and lays out. The engine calls this in
    // FramePhase::Update, immediately after dispatching host frame-update work,
    // so a semantic action drained this frame is visible in the same frame. That
    // ordering is the engine's to guarantee and not a matter of who registered
    // a system first.
    void Update();

    // -- inspection ----------------------------------------------------------

    // The measured content box of an element, after layout. Nullopt when the
    // screen is closed or no element carries that id.
    //
    // Public because it is how a test asserts that a document laid out the way
    // it was authored to, without a window or a device -- the substrate's own
    // regression surface.
    [[nodiscard]] std::optional<UiElementBox> MeasureElement(UiScreenHandle screen,
                                                            std::string_view elementId) const;

private:
    // The document engine, and the only thing that knows what implements it.
    std::unique_ptr<UiRuntime> Runtime;
};
