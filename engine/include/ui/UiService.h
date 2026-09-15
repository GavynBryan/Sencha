#pragma once

#include <graphics/RenderExtent.h>
#include <input/UiInputCapture.h>
#include <render/ui/UiDrawFrame.h>
#include <ui/UiAction.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

class AssetSystem;
class FontFaceCache;
class LoggingProvider;
class TextureCache;
class UiPackageCache;
class UiRuntime;
struct SDL_Window;

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
    // `textures` may be null: a process with no texture cache simply cannot
    // resolve content images, and says so when a document asks for one.
    // `window` is what text input is requested against. Null in a process with
    // no window: documents still load and lay out, and a text field simply
    // never asks the platform for an input method.
    UiService(LoggingProvider& logging,
              AssetSystem& assets,
              UiPackageCache& packages,
              FontFaceCache& fonts,
              TextureCache* textures = nullptr,
              SDL_Window* window = nullptr);
    ~UiService();

    UiService(const UiService&) = delete;
    UiService& operator=(const UiService&) = delete;
    UiService(UiService&&) = delete;
    UiService& operator=(UiService&&) = delete;

    // Closes every screen and surface, releasing the asset leases they hold.
    //
    // A host MUST call this while the asset caches are still alive -- from
    // Game::OnShutdown, not from wherever its UiService member happens to be
    // destroyed. A lease outliving the cache it references calls Detach on a
    // destroyed owner, which is a pure-virtual call, not a leak: it takes the
    // process down at exit and points nowhere near the cause.
    //
    // Idempotent, and the destructor calls it too, so a host whose ordering is
    // already correct needs nothing extra.
    void Shutdown();

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

    // Display scale, as a ratio against authored lengths: 2.0 makes a 100px
    // panel cover 200 physical pixels. Live, not latched at startup -- a
    // retained document re-flows when it changes, which is exactly what a baked
    // immediate-mode font atlas cannot do. Clamped to a sane range.
    void SetSurfaceScale(UiSurfaceId surface, float scale);
    [[nodiscard]] float GetSurfaceScale(UiSurfaceId surface) const;

    // -- screens -------------------------------------------------------------

    // Opens a screen: the cooked package, what it presents, and what it can ask
    // for, all declared together because the document engine binds a model
    // before it parses the document that reads it.
    //
    // The screen takes its own lease on the package and on every resource the
    // package's table names, and drops them when it closes.
    //
    // Invalid handle if the package is missing, is not a cooked .sui, names a
    // resource that cannot be resolved, or declares a model the document engine
    // refuses. The failure is logged once with the authoring detail; callers
    // check the handle rather than an error code.
    [[nodiscard]] UiScreenHandle OpenScreen(UiSurfaceId surface, const UiScreenDesc& desc);

    // A package with nothing to present and nothing to ask for.
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

    // -- input ---------------------------------------------------------------

    // Offers one platform event to the UI. True when a surface consumed it,
    // which the frame pump uses to stop it reaching consumers below.
    //
    // Raw events on purpose: pointer position, wheel, key editing, text and IME
    // are presentation concerns that need the fidelity the device reported, and
    // an action mapping would have thrown most of it away. What gameplay hears
    // is decided separately, by an InputContextLease the host takes.
    [[nodiscard]] bool ProcessPlatformEvent(const union SDL_Event& event);

    // Which devices the UI is consuming. Published, never compensated for: the
    // device snapshot still records everything that happened, and this is what
    // tells a reader of raw state which of it was not meant for them.
    [[nodiscard]] UiInputCapture Capture() const;

    // Abstract navigation, from whatever the host mapped it to. A document
    // names no key and no gamepad button, so remapping, controller profiles and
    // accessibility settings keep working without knowing a document exists.
    void Navigate(UiSurfaceId surface, UiNavigation direction);

    // Records every live surface into an immutable draw frame, in
    // ExtractRender. No GPU work happens here; the frames stay valid until the
    // next call, which is what the render feature reads.
    void ExtractRender();

    // This frame's recorded UI, one entry per surface that drew anything.
    //
    // Handed to the render feature by reference, the same way the render
    // pipeline publishes its own extracted state: valid from ExtractRender
    // until the next one, and self-contained, so reading it never reaches back
    // into a document.
    [[nodiscard]] const std::vector<UiDrawFrame>& Frames() const;

    // -- presentation model --------------------------------------------------

    // Publishes a value. Returns whether it changed: an unchanged set costs a
    // comparison and marks nothing dirty, so a host may publish every frame
    // without re-evaluating every binding that reads the property.
    bool SetValue(UiScreenHandle screen, UiModelPropertyId property, UiValue value);
    [[nodiscard]] UiValue GetValue(UiScreenHandle screen, UiModelPropertyId property) const;

    // Resolve once and keep the id. Both are linear over a screen's declared
    // list, which is short and walked at setup, never per frame.
    [[nodiscard]] UiModelPropertyId FindProperty(UiScreenHandle screen,
                                                 std::string_view path) const;
    [[nodiscard]] UiActionId FindAction(UiScreenHandle screen, std::string_view name) const;

    // -- semantic actions ----------------------------------------------------

    // Takes everything documents have asked for since the last call.
    //
    // Drained by host controllers at the top of the frame's update, before the
    // engine updates the UI -- so an action, the state change it causes, and
    // the republished value all land in one frame rather than three.
    //
    // The returned actions are owned copies. Nothing in one refers to a
    // document, an element, or anything the application owns.
    [[nodiscard]] std::vector<UiAction> DrainActions();

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
