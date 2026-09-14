#pragma once

#include <core/assets/AssetLease.h>
#include <core/logging/Logger.h>
#include <graphics/RenderExtent.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include "rml/RmlRenderRecorder.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Rml
{
class Context;
class ElementDocument;
}

class AssetSystem;
class FontFaceCache;
class LoggingProvider;
class RmlPackageFileSource;
class RmlSystemBridge;
class TextureCache;
class UiPackageCache;

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
class UiRuntime final : public IUiTextureResolver
{
public:
    // `textures` is null in a process with no texture cache, which makes content
    // images unresolvable rather than being an error in itself -- the same
    // posture the asset layer takes toward a kind this composition cannot hold.
    UiRuntime(LoggingProvider& logging,
              AssetSystem& assets,
              UiPackageCache& packages,
              FontFaceCache& fonts,
              TextureCache* textures);
    ~UiRuntime();

    UiRuntime(const UiRuntime&) = delete;
    UiRuntime& operator=(const UiRuntime&) = delete;
    UiRuntime(UiRuntime&&) = delete;
    UiRuntime& operator=(UiRuntime&&) = delete;

    [[nodiscard]] bool IsReady() const { return Ready; }

    [[nodiscard]] UiSurfaceId CreateSurface(std::string_view name, RenderExtent size);
    void DestroySurface(UiSurfaceId surface);
    void SetSurfaceSize(UiSurfaceId surface, RenderExtent size);
    [[nodiscard]] RenderExtent GetSurfaceSize(UiSurfaceId surface) const;

    void SetSurfaceScale(UiSurfaceId surface, float scale);
    [[nodiscard]] float GetSurfaceScale(UiSurfaceId surface) const;

    [[nodiscard]] UiScreenHandle OpenScreen(UiSurfaceId surface, std::string_view packagePath);
    void CloseScreen(UiScreenHandle screen);
    [[nodiscard]] bool IsScreenOpen(UiScreenHandle screen) const;

    void Update();

    // Records every live surface into an immutable draw frame. Runs in
    // ExtractRender: no GPU work, and the frames stay valid until the next call.
    void ExtractRender();
    [[nodiscard]] const std::vector<UiDrawFrame>& Frames() const { return DrawFrames; }

    // IUiTextureResolver: a content image resolves against the open screen's
    // own resource table and nothing else.
    [[nodiscard]] bool ResolveTexture(std::string_view source,
                                      TextureHandle& outHandle,
                                      RenderExtent& outSize) override;

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
        std::unordered_map<std::string, TextureHandle>& outTextures);

    void CloseScreenSlot(Screen& screen);

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
    std::unique_ptr<RmlRenderRecorder> Recorder;

    std::vector<Surface> Surfaces;
    std::vector<Screen> Screens;

    // Rebuilt every extract, one per live surface, and handed to the render
    // feature by reference -- the same publication shape the render pipeline
    // uses for its own extracted state.
    std::vector<UiDrawFrame> DrawFrames;

    // Whose resource table answers an image request. Set while a document is
    // loading or rendering, null otherwise, so a request arriving outside both
    // fails instead of resolving against whatever was open last.
    const Screen* ActiveScreen = nullptr;

    // Faces already handed to the document engine, by asset path. The engine
    // takes a copy of the bytes and files the face under its family, so
    // registering the same path twice would shadow the first registration with
    // an identical one.
    std::vector<std::string> RegisteredFonts;

    bool Ready = false;
};
