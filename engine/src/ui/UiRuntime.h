#pragma once

#include <core/assets/AssetLease.h>
#include <core/logging/Logger.h>
#include <graphics/RenderExtent.h>
#include <assets/font/FontFaceHandle.h>
#include <input/UiInputCapture.h>
#include <ui/UiAction.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include "rml/RmlPackageFileSource.h"
#include "rml/RmlTextInputBridge.h"
#include "rml/RmlRenderRecorder.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Rml
{
class Context;
class ElementDocument;
class DataModelHandle;
}

class AssetSystem;
class FontFaceCache;
class LoggingProvider;
class RmlPackageFileSource;
class RmlSystemBridge;
class TextureCache;
class UiPackageCache;
struct SDL_Window;

//=============================================================================
// UiRuntime
//
// The authored UI layer's engine-side implementation: document engine lifetime,
// contexts, the screens open on each, and the leases those screens hold.
//
// Private on purpose. UiService is what a host talks to, and this is the only
// translation unit family that knows a document engine is involved at all.
//
// Process-global, because the document engine's interfaces are: there is one
// system interface, one file interface, one render interface. Constructing two
// of these in one process would have the second quietly steal the first's
// interfaces, so construction refuses rather than allowing it.
//=============================================================================
class UiRuntime final : public IUiTextureResolver, public IUiPackageResourceBytes
{
public:
    // `textures` is null in a process with no texture cache, which makes content
    // images unresolvable rather than being an error in itself -- the same
    // posture the asset layer takes toward a kind this composition cannot hold.
    UiRuntime(LoggingProvider& logging,
              AssetSystem& assets,
              UiPackageCache& packages,
              FontFaceCache& fonts,
              TextureCache* textures,
              SDL_Window* window);
    ~UiRuntime();

    UiRuntime(const UiRuntime&) = delete;
    UiRuntime& operator=(const UiRuntime&) = delete;
    UiRuntime(UiRuntime&&) = delete;
    UiRuntime& operator=(UiRuntime&&) = delete;

    [[nodiscard]] bool IsReady() const { return Ready; }

    // Releases everything the runtime holds on behalf of a host: screens, their
    // leases, and the contexts. Separate from the destructor because the leases
    // have to go while the caches they reference are still alive, and a host's
    // member destruction order is not something this layer can decide.
    void Shutdown();

    [[nodiscard]] UiSurfaceId CreateSurface(std::string_view name, RenderExtent size);
    void DestroySurface(UiSurfaceId surface);
    void SetSurfaceSize(UiSurfaceId surface, RenderExtent size);
    [[nodiscard]] RenderExtent GetSurfaceSize(UiSurfaceId surface) const;

    void SetSurfaceScale(UiSurfaceId surface, float scale);
    [[nodiscard]] float GetSurfaceScale(UiSurfaceId surface) const;

    [[nodiscard]] UiScreenHandle OpenScreen(UiSurfaceId surface, const UiScreenDesc& desc);
    void CloseScreen(UiScreenHandle screen);
    [[nodiscard]] bool IsScreenOpen(UiScreenHandle screen) const;

    void Update();

    // Offers one platform event to the surfaces, topmost first. True when a
    // surface consumed it, which stops it reaching anything below.
    [[nodiscard]] bool ProcessPlatformEvent(const union SDL_Event& event);

    // What the UI is currently consuming, for a reader of raw device state.
    [[nodiscard]] UiInputCapture Capture() const;

    // Abstract navigation, driven by the host's own mapped actions rather than
    // by keys this layer decided on. The document engine's focus and spatial
    // navigation do the work; where the input came from stays Sencha's.
    void Navigate(UiSurfaceId surface, UiNavigation direction);

    [[nodiscard]] bool SetValue(UiScreenHandle screen, UiModelPropertyId property, UiValue value);
    [[nodiscard]] bool SetArray(UiScreenHandle screen, UiModelArrayId array,
                                std::span<const std::string> items);
    [[nodiscard]] std::size_t ArraySize(UiScreenHandle screen, UiModelArrayId array) const;
    [[nodiscard]] UiModelArrayId FindArray(UiScreenHandle screen, std::string_view path) const;
    [[nodiscard]] UiValue GetValue(UiScreenHandle screen, UiModelPropertyId property) const;
    [[nodiscard]] UiModelPropertyId FindProperty(UiScreenHandle screen,
                                                 std::string_view path) const;
    [[nodiscard]] UiActionId FindAction(UiScreenHandle screen, std::string_view name) const;

    [[nodiscard]] std::vector<UiAction> DrainActions();

    // Records every live surface into an immutable draw frame. Runs in
    // ExtractRender: no GPU work, and the frames stay valid until the next call.
    void ExtractRender();
    [[nodiscard]] const std::vector<UiDrawFrame>& Frames() const { return DrawFrames; }

    // IUiTextureResolver: a content image resolves against the open screen's
    // own resource table and nothing else.
    [[nodiscard]] bool ResolveTexture(std::string_view source,
                                      TextureHandle& outHandle,
                                      RenderExtent& outSize) override;

    // IUiPackageResourceBytes: a font face the open screen leases. Faces are
    // referenced rather than packaged, so this is how the document engine's
    // own @font-face handling reaches one.
    [[nodiscard]] bool ResolveResourceBytes(std::string_view source,
                                            const std::vector<std::byte>*& outBytes) override;

    [[nodiscard]] std::optional<UiElementBox> MeasureElement(UiScreenHandle screen,
                                                             std::string_view elementId) const;

    // Outstanding document-engine resources, for a test that wants to prove a
    // closed screen left nothing behind.
    [[nodiscard]] std::uint32_t LiveGeometryCount() const;
    [[nodiscard]] std::uint32_t LiveTextureCount() const;

private:
    // A surface and its context. Slots are never compacted; a destroyed surface
    // leaves a dead slot whose generation has moved on.
    struct Surface
    {
        std::string Name;
        RenderExtent Size{};
        float Scale = 1.0f;
        Rml::Context* Context = nullptr;
        std::uint32_t Generation = 1;
        bool Live = false;
    };

    // One open document, and everything whose lifetime it decides.
    //
    // The leases are the point. A screen holds its package and every resource
    // that package's table names, so closing it releases exactly what opening
    // it acquired -- and two screens built from one package each hold their own,
    // rather than racing over a reference the cache would have to arbitrate.
    // One bound property: its declared path, the value the document currently
    // reads, and whether that value changed since the document last saw it.
    //
    // Storage lives here rather than in the document engine because a set has
    // to compare against the previous value to decide whether anything is
    // dirty -- and a model that marks everything dirty every frame re-evaluates
    // every binding that reads it, which is the cost this exists to avoid.
    struct BoundProperty
    {
        std::string Path;
        UiValue Value;
        bool Editable = false;
    };

    struct Screen
    {
        UiSurfaceId Surface;
        Rml::ElementDocument* Document = nullptr;
        AssetLease Package;
        std::vector<AssetLease> Resources;

        // Where a relative image reference in this document resolves from: the
        // directory its root markup was cooked from. The cooker resolved the
        // resource table the same way, so the two agree by construction.
        std::string ResourceRoot;
        std::unordered_map<std::string, TextureHandle> TexturesByAssetPath;
        std::unordered_map<std::string, FontFaceHandle> FontsByAssetPath;

        bool Modal = false;
        std::string ModelName;
        std::vector<BoundProperty> Properties;

        // Each list gets its own allocation because the document engine binds
        // an array by address: a vector of vectors would move its elements on
        // growth and leave every binding pointing at freed storage.
        struct BoundArray
        {
            std::string Path;
            std::vector<std::string> Items;
        };
        std::vector<std::unique_ptr<BoundArray>> Arrays;
        std::vector<std::string> ActionNames;
        // Null until a model is constructed, which only happens for a screen
        // that declared one.
        std::unique_ptr<Rml::DataModelHandle> Model;

        // What the open document was built from, and everything needed to
        // build it again. A reload rebuilds the document; it must not make the
        // host re-describe a screen it already described.
        std::string PackagePath;
        std::uint64_t PackageVersion = 0;
        UiScreenDesc Description;

        std::uint32_t Generation = 1;
        bool Live = false;
    };

    [[nodiscard]] Surface* ResolveSurface(UiSurfaceId surface);
    [[nodiscard]] const Surface* ResolveSurface(UiSurfaceId surface) const;
    [[nodiscard]] Screen* ResolveScreen(UiScreenHandle screen);
    [[nodiscard]] const Screen* ResolveScreen(UiScreenHandle screen) const;

    // Acquires a lease on every resource the package names, and registers any
    // font among them with the document engine. All-or-nothing: a package that
    // names a resource this process cannot resolve does not open half-dressed.
    [[nodiscard]] bool AcquireResources(
        const struct UiPackage& package,
        std::string_view packagePath,
        std::vector<AssetLease>& outLeases,
        std::unordered_map<std::string, TextureHandle>& outTextures,
        std::unordered_map<std::string, FontFaceHandle>& outFonts);

    // The asset path a document-relative reference names, resolved the way the
    // cooker resolved the resource table so the two agree by construction.
    [[nodiscard]] std::string AssetPathFor(const Screen& screen, std::string_view source) const;

    void CloseScreenSlot(Screen& screen);

    // Rebuilds any open document whose package has been reloaded underneath it,
    // carrying across the state the document itself does not own.
    void ReloadChangedScreens();
    [[nodiscard]] bool RebuildScreen(Screen& screen, UiScreenHandle handle);

    // Faces a document declared and the engine has already ingested. A face
    // whose bytes changed has to be dropped before a rebuild re-requests it, or
    // the engine answers from what it parsed the first time.
    void ForgetFontResources();

    // Constructs the data model a screen's document will bind to. Must run
    // before the document loads: the engine resolves a document's data-model
    // attribute at parse, and a model that appears afterwards is a model the
    // document never saw.
    [[nodiscard]] bool BuildModel(Screen& screen, UiScreenHandle handle, Surface& surface);

    Logger& Log;
    AssetSystem& Assets;
    UiPackageCache& Packages;
    FontFaceCache& Fonts;
    TextureCache* Textures = nullptr;

    // Declared before the slots: contexts and documents are released in the
    // destructor body, and the interfaces they call into must still be alive
    // when they are.
    std::unique_ptr<RmlSystemBridge> SystemBridge;
    std::unique_ptr<RmlPackageFileSource> FileSource;
    std::unique_ptr<RmlTextInputBridge> TextInput;
    std::unique_ptr<RmlRenderRecorder> Recorder;

    std::vector<Surface> Surfaces;
    std::vector<Screen> Screens;

    // What documents asked for since the host last drained. Owned copies, so a
    // host can hold them across the frame boundary that produced them.
    std::vector<UiAction> PendingActions;

    // Rebuilt every extract, one per live surface, and handed to the render
    // feature by reference -- the same publication shape the render pipeline
    // uses for its own extracted state.
    std::vector<UiDrawFrame> DrawFrames;

    // Whose resource table answers an image request. Set while a document is
    // loading or rendering, null otherwise, so a request arriving outside both
    // fails instead of resolving against whatever was open last.
    const Screen* ActiveScreen = nullptr;

    // Last reported pointer position, in window pixels. The document engine is
    // told where the pointer is on a move; a click has to be told the same
    // thing again, because a button press with no preceding move (a synthetic
    // event, a touch tap) would otherwise land wherever the pointer last was.
    int PointerX = 0;
    int PointerY = 0;
    bool PointerInside = false;

    bool Ready = false;
};
