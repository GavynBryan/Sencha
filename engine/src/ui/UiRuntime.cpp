#include "UiRuntime.h"

#include "rml/RmlRenderRecorder.h"
#include "rml/RmlPackageFileSource.h"
#include "rml/RmlSystemBridge.h"

#include <RmlUi/Core.h>

#include <assets/font/FontFace.h>
#include <assets/font/FontFaceCache.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/texture/TextureCache.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageCache.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <utility>

namespace
{
// The document engine's interfaces are process-global, so two runtimes in one
// process would silently fight over them. Refusing the second is a clearer
// failure than the corruption that follows from allowing it.
std::atomic<bool> g_RuntimeLive{ false };

Rml::Vector2i ToRmlDimensions(RenderExtent size)
{
    return Rml::Vector2i(static_cast<int>(size.Width), static_cast<int>(size.Height));
}


} // namespace

UiRuntime::UiRuntime(LoggingProvider& logging,
                     AssetSystem& assets,
                     UiPackageCache& packages,
                     FontFaceCache& fonts,
                     TextureCache* textures)
    : Log(logging.GetLogger<UiRuntime>())
    , Assets(assets)
    , Packages(packages)
    , Fonts(fonts)
    , Textures(textures)
{
    bool expected = false;
    if (!g_RuntimeLive.compare_exchange_strong(expected, true))
    {
        Log.Error("UiRuntime: a runtime already exists in this process; "
                  "the document engine's interfaces are global and cannot be shared");
        return;
    }

    SystemBridge = std::make_unique<RmlSystemBridge>(Log);
    FileSource = std::make_unique<RmlPackageFileSource>(Log);
    Recorder = std::make_unique<RmlRenderRecorder>(Log);

    Rml::SetSystemInterface(SystemBridge.get());
    Rml::SetFileInterface(FileSource.get());
    Recorder->SetTextureResolver(this);
    FileSource->SetResourceBytes(this);
    Rml::SetRenderInterface(Recorder.get());

    if (!Rml::Initialise())
    {
        Log.Error("UiRuntime: the document engine failed to initialise; "
                  "authored UI is unavailable this session");
        Rml::SetSystemInterface(nullptr);
        Rml::SetFileInterface(nullptr);
        Rml::SetRenderInterface(nullptr);
        SystemBridge.reset();
        FileSource.reset();
        Recorder.reset();
        g_RuntimeLive.store(false);
        return;
    }

    Ready = true;
}

void UiRuntime::Shutdown()
{
    if (!Ready)
        return;

    // Screens before surfaces. A document outliving its context is a
    // use-after-free inside the document engine rather than a leak we would
    // hear about.
    for (Screen& screen : Screens)
    {
        if (screen.Live)
            CloseScreenSlot(screen);
    }
    Screens.clear();

    for (Surface& surface : Surfaces)
    {
        if (surface.Live && surface.Context != nullptr)
            Rml::RemoveContext(surface.Context->GetName());
    }
    Surfaces.clear();
    DrawFrames.clear();
    ActiveScreen = nullptr;
}

UiRuntime::~UiRuntime()
{
    if (!Ready)
        return;

    // Idempotent, and a no-op for a host that shut down in the right order --
    // which it had to, because the leases released in here reference caches
    // this object does not own and cannot outlive.
    Shutdown();

    Rml::Shutdown();

    Rml::SetSystemInterface(nullptr);
    Rml::SetFileInterface(nullptr);
    Rml::SetRenderInterface(nullptr);

    Recorder.reset();
    FileSource.reset();
    SystemBridge.reset();

    g_RuntimeLive.store(false);
}

// -- surfaces ----------------------------------------------------------------

UiSurfaceId UiRuntime::CreateSurface(std::string_view name, RenderExtent size)
{
    if (!Ready)
        return {};
    if (size.Width == 0 || size.Height == 0)
    {
        Log.Error("UiRuntime: surface '{}' needs a nonzero size", name);
        return {};
    }

    // Reuse a dead slot if there is one, bumping its generation so handles to
    // the surface that used to live there stay stale.
    std::size_t index = Surfaces.size();
    for (std::size_t i = 0; i < Surfaces.size(); ++i)
    {
        if (!Surfaces[i].Live)
        {
            index = i;
            break;
        }
    }
    if (index == Surfaces.size())
        Surfaces.emplace_back();

    Surface& surface = Surfaces[index];
    // Context names are the engine's own key, so they have to be unique across
    // slots and across reuses of one slot.
    std::string contextName = std::string(name) + "#" + std::to_string(index)
        + "." + std::to_string(surface.Generation);

    Rml::Context* context = Rml::CreateContext(contextName, ToRmlDimensions(size));
    if (context == nullptr)
    {
        Log.Error("UiRuntime: could not create a context for surface '{}'", name);
        return {};
    }

    surface.Name = std::move(contextName);
    surface.Size = size;
    surface.Context = context;
    surface.Live = true;

    return UiSurfaceId{ static_cast<std::uint32_t>(index + 1), surface.Generation };
}

void UiRuntime::DestroySurface(UiSurfaceId surface)
{
    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr)
        return;

    // Every screen on it goes first: a document outliving its context is a
    // dangling pointer inside the engine.
    for (Screen& screen : Screens)
    {
        if (screen.Live && screen.Surface == surface)
            CloseScreenSlot(screen);
    }

    if (slot->Context != nullptr)
        Rml::RemoveContext(slot->Name);

    slot->Context = nullptr;
    slot->Live = false;
    slot->Name.clear();
    ++slot->Generation;
}

void UiRuntime::SetSurfaceSize(UiSurfaceId surface, RenderExtent size)
{
    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr || size.Width == 0 || size.Height == 0)
        return;
    if (slot->Size.Width == size.Width && slot->Size.Height == size.Height)
        return;

    slot->Size = size;
    slot->Context->SetDimensions(ToRmlDimensions(size));
}

RenderExtent UiRuntime::GetSurfaceSize(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr ? slot->Size : RenderExtent{};
}

void UiRuntime::SetSurfaceScale(UiSurfaceId surface, float scale)
{
    Surface* slot = ResolveSurface(surface);
    // Clamped rather than trusted: this comes from a display probe or a user
    // setting, and a zero or negative ratio collapses every authored length to
    // nothing with no obvious cause.
    const float clamped = std::clamp(scale, 0.25f, 8.0f);
    if (slot == nullptr || slot->Scale == clamped)
        return;

    slot->Scale = clamped;
    // Every authored length re-resolves against this, which is the whole reason
    // UI scale is live here and latched at startup for the ImGui shell: a
    // retained document re-flows, a baked font atlas cannot.
    slot->Context->SetDensityIndependentPixelRatio(clamped);
}

float UiRuntime::GetSurfaceScale(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr ? slot->Scale : 0.0f;
}

// -- screens -----------------------------------------------------------------

bool UiRuntime::AcquireResources(const UiPackage& package,
                                 std::string_view packagePath,
                                 std::vector<AssetLease>& outLeases,
                                 std::unordered_map<std::string, TextureHandle>& outTextures,
                                 std::unordered_map<std::string, FontFaceHandle>& outFonts)
{
    outLeases.clear();
    outTextures.clear();
    outFonts.clear();
    outLeases.reserve(package.Resources.size());

    for (const AssetRef& resource : package.Resources)
    {
        AssetLease lease = Assets.LoadLease(resource.Path, resource.Type);
        if (!lease.IsValid())
        {
            Log.Error("UiRuntime: package '{}' names {} '{}', which did not load",
                      packagePath, AssetTypeToString(resource.Type), resource.Path);
            // All-or-nothing: a half-dressed document lays out against fonts it
            // does not have and measures wrong, which is harder to diagnose than
            // a refusal.
            outLeases.clear();
            return false;
        }

        if (resource.Type == AssetType::Font)
        {
            // Not registered here. A document declares its faces in RCSS, and
            // the document engine loads them through the file interface, which
            // is how it learns the family, weight and style the author wrote.
            // Registering them a second time from the cooked metadata would be
            // a competing source of truth for what a face is called.
            outFonts.emplace(resource.Path, FontFaceHandle::FromToken(lease.OpaqueToken()));
        }
        else if (resource.Type == AssetType::Texture)
        {
            outTextures.emplace(resource.Path, TextureHandle::FromToken(lease.OpaqueToken()));
        }

        outLeases.push_back(std::move(lease));
    }

    return true;
}

UiScreenHandle UiRuntime::OpenScreen(UiSurfaceId surface, std::string_view packagePath)
{
    if (!Ready)
        return {};

    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr)
    {
        Log.Error("UiRuntime: cannot open '{}' on a surface that does not exist", packagePath);
        return {};
    }

    AssetLease packageLease = Assets.LoadLease(packagePath, AssetType::UiPackage);
    if (!packageLease.IsValid())
    {
        Log.Error("UiRuntime: UI package '{}' did not load", packagePath);
        return {};
    }

    const UiPackage* package = Packages.Get(UiPackageHandle::FromToken(packageLease.OpaqueToken()));
    if (package == nullptr)
    {
        Log.Error("UiRuntime: UI package '{}' loaded but is not resident", packagePath);
        return {};
    }

    std::vector<AssetLease> resourceLeases;
    std::unordered_map<std::string, TextureHandle> screenTextures;
    std::unordered_map<std::string, FontFaceHandle> screenFonts;
    if (!AcquireResources(*package, packagePath, resourceLeases, screenTextures, screenFonts))
        return {};

    // Built before the load, because the document engine resolves images while
    // parsing and the resolver has to have something to answer from.
    Screen pending;
    pending.Surface = surface;
    pending.ResourceRoot = package->Blobs.empty()
        ? std::string{}
        : std::filesystem::path(package->Blobs.front().SourcePath).parent_path().generic_string();
    pending.TexturesByAssetPath = std::move(screenTextures);
    pending.FontsByAssetPath = std::move(screenFonts);

    Rml::ElementDocument* document = nullptr;
    {
        // The file interface answers from this package, and the resolver from
        // this screen, for exactly as long as the load runs -- and from nothing
        // at all outside it.
        const RmlPackageFileSource::ActivePackageScope scope(*FileSource, *package);
        ActiveScreen = &pending;
        document = slot->Context->LoadDocument(package->RootDocumentName);
        ActiveScreen = nullptr;
    }

    if (document == nullptr)
    {
        Log.Error("UiRuntime: '{}' did not parse into a document", packagePath);
        return {};
    }

    document->Show();

    std::size_t index = Screens.size();
    for (std::size_t i = 0; i < Screens.size(); ++i)
    {
        if (!Screens[i].Live)
        {
            index = i;
            break;
        }
    }
    if (index == Screens.size())
        Screens.emplace_back();

    Screen& screen = Screens[index];
    const std::uint32_t generation = screen.Generation;
    screen = std::move(pending);
    screen.Generation = generation;
    screen.Document = document;
    screen.Package = std::move(packageLease);
    screen.Resources = std::move(resourceLeases);
    screen.Live = true;

    return UiScreenHandle{ static_cast<std::uint32_t>(index + 1), screen.Generation };
}

void UiRuntime::CloseScreenSlot(Screen& screen)
{
    if (screen.Document != nullptr)
    {
        // Close() unloads it from its context. The context owns the document, so
        // nothing here deletes it.
        screen.Document->Close();
        screen.Document = nullptr;
    }

    // After the document, so a resource is released only once nothing is laid
    // out against it.
    screen.TexturesByAssetPath.clear();
    screen.FontsByAssetPath.clear();
    screen.Resources.clear();
    screen.Package.Reset();
    screen.Live = false;
    ++screen.Generation;
}

void UiRuntime::CloseScreen(UiScreenHandle screen)
{
    Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return;
    CloseScreenSlot(*slot);
}

bool UiRuntime::IsScreenOpen(UiScreenHandle screen) const
{
    return ResolveScreen(screen) != nullptr;
}

// -- frame -------------------------------------------------------------------

void UiRuntime::Update()
{
    if (!Ready)
        return;

    for (Surface& surface : Surfaces)
    {
        if (surface.Live && surface.Context != nullptr)
            surface.Context->Update();
    }
}

void UiRuntime::ExtractRender()
{
    DrawFrames.clear();
    if (!Ready)
        return;

    for (std::size_t index = 0; index < Surfaces.size(); ++index)
    {
        const Surface& surface = Surfaces[index];
        if (!surface.Live || surface.Context == nullptr)
            continue;

        const UiSurfaceId surfaceId{ static_cast<std::uint32_t>(index + 1), surface.Generation };

        // Whose resource table answers an image request while this surface
        // renders. One screen per surface today; a surface carrying a stack
        // resolves against the screen owning the command, which is why this is
        // scoped around the render rather than set once at open.
        ActiveScreen = nullptr;
        for (const Screen& screen : Screens)
        {
            if (screen.Live && screen.Surface == surfaceId && screen.Document != nullptr)
            {
                ActiveScreen = &screen;
                break;
            }
        }

        Recorder->BeginFrame(surface.Size);
        surface.Context->Render();
        ActiveScreen = nullptr;

        UiDrawFrame frame = Recorder->EndFrame();
        if (!frame.IsEmpty())
            DrawFrames.push_back(std::move(frame));
    }
}

std::string UiRuntime::AssetPathFor(const Screen& screen, std::string_view source) const
{
    // A document names a resource the way its author wrote it; the resource
    // table holds asset paths. The cooker resolved references against the root
    // document's directory, so resolving the same way here is what makes the
    // two agree by construction rather than by coincidence.
    std::filesystem::path combined = screen.ResourceRoot.empty()
        ? std::filesystem::path(source)
        : std::filesystem::path(screen.ResourceRoot) / std::filesystem::path(source);
    return "asset://" + combined.lexically_normal().generic_string();
}

bool UiRuntime::ResolveTexture(std::string_view source,
                               TextureHandle& outHandle,
                               RenderExtent& outSize)
{
    if (ActiveScreen == nullptr || Textures == nullptr)
        return false;

    const std::string assetPath = AssetPathFor(*ActiveScreen, source);
    const auto it = ActiveScreen->TexturesByAssetPath.find(assetPath);
    if (it == ActiveScreen->TexturesByAssetPath.end())
        return false;

    outHandle = it->second;
    outSize = Textures->GetExtent(it->second);
    return true;
}

bool UiRuntime::ResolveResourceBytes(std::string_view source,
                                     const std::vector<std::byte>*& outBytes)
{
    if (ActiveScreen == nullptr)
        return false;

    const std::string assetPath = AssetPathFor(*ActiveScreen, source);
    const auto it = ActiveScreen->FontsByAssetPath.find(assetPath);
    if (it == ActiveScreen->FontsByAssetPath.end())
        return false;

    const FontFace* face = Fonts.Get(it->second);
    if (face == nullptr)
        return false;

    // The face bytes out of the cooked container, never a path: a shipped build
    // has no font file on disk to find.
    outBytes = &face->Bytes;
    return true;
}

// -- inspection --------------------------------------------------------------

std::optional<UiElementBox> UiRuntime::MeasureElement(UiScreenHandle screen,
                                                      std::string_view elementId) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || slot->Document == nullptr)
        return std::nullopt;

    Rml::Element* element = slot->Document->GetElementById(std::string(elementId));
    if (element == nullptr)
        return std::nullopt;

    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Content);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Content);
    return UiElementBox{ offset.x, offset.y, size.x, size.y };
}

std::uint32_t UiRuntime::LiveGeometryCount() const
{
    return Recorder != nullptr ? Recorder->LiveGeometryCount() : 0;
}

std::uint32_t UiRuntime::LiveTextureCount() const
{
    return Recorder != nullptr ? Recorder->LiveTextureCount() : 0;
}

// -- slot resolution ---------------------------------------------------------

UiRuntime::Surface* UiRuntime::ResolveSurface(UiSurfaceId surface)
{
    return const_cast<Surface*>(std::as_const(*this).ResolveSurface(surface));
}

const UiRuntime::Surface* UiRuntime::ResolveSurface(UiSurfaceId surface) const
{
    if (!surface.IsValid() || surface.Index == 0 || surface.Index > Surfaces.size())
        return nullptr;
    const Surface& slot = Surfaces[surface.Index - 1];
    if (!slot.Live || slot.Generation != surface.Generation)
        return nullptr;
    return &slot;
}

UiRuntime::Screen* UiRuntime::ResolveScreen(UiScreenHandle screen)
{
    return const_cast<Screen*>(std::as_const(*this).ResolveScreen(screen));
}

const UiRuntime::Screen* UiRuntime::ResolveScreen(UiScreenHandle screen) const
{
    if (!screen.IsValid() || screen.Index == 0 || screen.Index > Screens.size())
        return nullptr;
    const Screen& slot = Screens[screen.Index - 1];
    if (!slot.Live || slot.Generation != screen.Generation)
        return nullptr;
    return &slot;
}
