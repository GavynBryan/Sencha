#include "UiRuntime.h"

#include "rml/RmlHeadlessRenderTarget.h"
#include "rml/RmlPackageFileSource.h"
#include "rml/RmlSystemBridge.h"

#include <RmlUi/Core.h>

#include <assets/font/FontFace.h>
#include <assets/font/FontFaceCache.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageCache.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>
#include <atomic>
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

Rml::Style::FontStyle ToRmlFontStyle(FontStyle style)
{
    return style == FontStyle::Italic ? Rml::Style::FontStyle::Italic
                                      : Rml::Style::FontStyle::Normal;
}

Rml::Style::FontWeight ToRmlFontWeight(std::uint16_t weight)
{
    return static_cast<Rml::Style::FontWeight>(weight);
}
} // namespace

UiRuntime::UiRuntime(LoggingProvider& logging,
                     AssetSystem& assets,
                     UiPackageCache& packages,
                     FontFaceCache& fonts)
    : Log(logging.GetLogger<UiRuntime>())
    , Assets(assets)
    , Packages(packages)
    , Fonts(fonts)
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
    RenderTarget = std::make_unique<RmlHeadlessRenderTarget>();

    Rml::SetSystemInterface(SystemBridge.get());
    Rml::SetFileInterface(FileSource.get());
    Rml::SetRenderInterface(RenderTarget.get());

    if (!Rml::Initialise())
    {
        Log.Error("UiRuntime: the document engine failed to initialise; "
                  "authored UI is unavailable this session");
        Rml::SetSystemInterface(nullptr);
        Rml::SetFileInterface(nullptr);
        Rml::SetRenderInterface(nullptr);
        SystemBridge.reset();
        FileSource.reset();
        RenderTarget.reset();
        g_RuntimeLive.store(false);
        return;
    }

    Ready = true;
}

UiRuntime::~UiRuntime()
{
    if (!Ready)
        return;

    // Screens before surfaces before the engine. A document outliving its
    // context, or either outliving Shutdown, is a use-after-free inside the
    // engine rather than a leak we would hear about.
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

    Rml::Shutdown();

    Rml::SetSystemInterface(nullptr);
    Rml::SetFileInterface(nullptr);
    Rml::SetRenderInterface(nullptr);

    RenderTarget.reset();
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

// -- screens -----------------------------------------------------------------

bool UiRuntime::AcquireResources(const UiPackage& package,
                                 std::string_view packagePath,
                                 std::vector<AssetLease>& outLeases)
{
    outLeases.clear();
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
            const bool already = std::find(RegisteredFonts.begin(), RegisteredFonts.end(),
                                           resource.Path) != RegisteredFonts.end();
            if (!already)
            {
                const FontFace* face = Fonts.Get(FontFaceHandle::FromToken(lease.OpaqueToken()));
                if (face == nullptr)
                {
                    Log.Error("UiRuntime: font '{}' loaded but is not resident", resource.Path);
                    outLeases.clear();
                    return false;
                }

                // Bytes, never a path: the engine is handed a span it copies,
                // so a shipped build needs no font file on disk.
                const Rml::Span<const Rml::byte> bytes(
                    reinterpret_cast<const Rml::byte*>(face->Bytes.data()), face->Bytes.size());
                if (!Rml::LoadFontFace(bytes, face->Family, ToRmlFontStyle(face->Style),
                                       ToRmlFontWeight(face->Weight), face->Fallback))
                {
                    Log.Error("UiRuntime: '{}' is not a face the font engine could read",
                              resource.Path);
                    outLeases.clear();
                    return false;
                }
                RegisteredFonts.emplace_back(resource.Path);
            }
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
    if (!AcquireResources(*package, packagePath, resourceLeases))
        return {};

    Rml::ElementDocument* document = nullptr;
    {
        // The file interface answers from this package for exactly as long as
        // the load runs, and from nothing at all outside it.
        const RmlPackageFileSource::ActivePackageScope scope(*FileSource, *package);
        document = slot->Context->LoadDocument(package->RootDocumentName);
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
    screen.Surface = surface;
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
    return RenderTarget != nullptr ? RenderTarget->LiveGeometryCount() : 0;
}

std::uint32_t UiRuntime::LiveTextureCount() const
{
    return RenderTarget != nullptr ? RenderTarget->LiveTextureCount() : 0;
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
