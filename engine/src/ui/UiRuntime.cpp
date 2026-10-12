#include "UiRuntime.h"

#include <platform/SdlWindowService.h>

#include <ui/UiSurfacePlacement.h>

#include "rml/RmlInputBridge.h"
#include "rml/RmlRenderRecorder.h"
#include "rml/RmlSectorElement.h"
#include "rml/RmlPackageFileSource.h"
#include "rml/RmlSystemBridge.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Factory.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>
#include <RmlUi/Core/DataModelHandle.h>

#include <assets/font/FontFace.h>
#include <assets/font/FontFaceCache.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/texture/TextureCache.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageCache.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <format>
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
                     TextureCache* textures,
                     SDL_Window* window)
    : Log(logging.GetLogger<UiRuntime>())
    , Assets(assets)
    , Packages(packages)
    , Fonts(fonts)
    , Textures(textures)
    , Diagnostics(Log)
{
    bool expected = false;
    if (!g_RuntimeLive.compare_exchange_strong(expected, true))
    {
        Log.Error("UiRuntime: a runtime already exists in this process; "
                  "the document engine's interfaces are global and cannot be shared");
        return;
    }

    SystemBridge = std::make_unique<RmlSystemBridge>(Diagnostics);
    FileSource = std::make_unique<RmlPackageFileSource>(Log);
    TextInput = std::make_unique<RmlTextInputBridge>(window);
    PrimaryWindow = window;
    PrimaryWindowId = window != nullptr ? SDL_GetWindowID(window) : 0;
    Recorder = std::make_unique<RmlRenderRecorder>(Log);

    Rml::SetSystemInterface(SystemBridge.get());
    Rml::SetFileInterface(FileSource.get());
    Rml::SetTextInputHandler(TextInput.get());
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
        Rml::SetTextInputHandler(nullptr);
        SystemBridge.reset();
        FileSource.reset();
        TextInput.reset();
        Recorder.reset();
        g_RuntimeLive.store(false);
        return;
    }
    RmlSectorElement::Register();

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
    OffscreenFrames.clear();
    PendingActions.clear();
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
    Rml::SetTextInputHandler(nullptr);

    Recorder.reset();
    TextInput.reset();
    FileSource.reset();
    SystemBridge.reset();

    g_RuntimeLive.store(false);
}

UiScreenHandle UiRuntime::HandleOf(const Screen& screen) const
{
    const auto index = static_cast<std::uint32_t>(&screen - Screens.data());
    return UiScreenHandle{ index + 1, screen.Generation };
}

UiRuntime::ActiveScreenScope::ActiveScreenScope(UiRuntime& runtime, const Screen& screen,
                                                UiScreenHandle handle)
    : Runtime(runtime)
    , Previous(runtime.ActiveScreen)
    , Attribution(runtime.Diagnostics, { handle, screen.Surface })
{
    Runtime.ActiveScreen = &screen;
}

UiRuntime::ActiveScreenScope::~ActiveScreenScope()
{
    Runtime.ActiveScreen = Previous;
}

void UiRuntime::Refuse(UiDiagnosticKind kind, std::optional<std::string> path, std::string message)
{
    UiDiagnostic entry;
    entry.Severity = UiDiagnosticSeverity::Error;
    entry.Source = UiDiagnosticSource::Runtime;
    entry.Kind = kind;
    entry.Path = std::move(path);
    entry.Message = std::move(message);
    Diagnostics.Push(std::move(entry));
}

std::vector<UiDiagnostic> UiRuntime::DrainDiagnostics()
{
    return Diagnostics.Drain();
}

UiDiagnosticLog::Attribution UiRuntime::SurfaceAttribution(std::size_t surfaceIndex) const
{
    const Surface& surface = Surfaces[surfaceIndex];
    const UiSurfaceId surfaceId{ static_cast<std::uint32_t>(surfaceIndex + 1), surface.Generation };
    UiScreenHandle only;
    std::size_t liveOnSurface = 0;
    for (const Screen& screen : Screens)
    {
        if (screen.Live && screen.Surface == surfaceId)
        {
            ++liveOnSurface;
            only = HandleOf(screen);
        }
    }
    return { liveOnSurface == 1 ? only : UiScreenHandle{}, surfaceId };
}

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

    context->GetRootElement()->AddEventListener(Rml::EventId::Mousescroll, &WheelZoom);
    surface.Name = std::move(contextName);
    surface.Size = size;
    surface.Context = context;
    surface.Live = true;
    surface.ModelTypesDeclared = false;
    surface.RowLists.reset();
    RebuildInputOrder();

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
    slot->ModelTypesDeclared = false;
    slot->RowLists.reset();
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
    // Every authored length re-resolves against this, which is why UI scale is
    // live: a retained document re-flows.
    slot->Context->SetDensityIndependentPixelRatio(clamped);
}

float UiRuntime::GetSurfaceScale(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr ? slot->Scale : 0.0f;
}

void UiRuntime::SetSurfacePlacement(UiSurfaceId surface, std::optional<Rect2d> windowRect)
{
    if (Surface* slot = ResolveSurface(surface))
        slot->Placement = windowRect;
}

std::optional<Rect2d> UiRuntime::GetSurfacePlacement(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr ? slot->Placement : std::nullopt;
}

void UiRuntime::SetSurfaceInputPolicy(UiSurfaceId surface, UiSurfaceInputPolicy policy)
{
    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr)
        return;
    // A surface that stops taking input is left the way a pointer leaving it
    // would leave it, so nothing stays hovered or held under a policy change.
    if (policy == UiSurfaceInputPolicy::Disabled && slot->InputPolicy != policy
        && slot->Context != nullptr)
    {
        EndPress(*slot);
        (void)slot->Context->ProcessMouseLeave();
        slot->PointerInside = false;
    }
    if (policy != UiSurfaceInputPolicy::Full && slot->Context != nullptr)
        DropFocus(*slot->Context);
    slot->InputPolicy = policy;
}

UiSurfaceInputPolicy UiRuntime::GetSurfaceInputPolicy(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr ? slot->InputPolicy : UiSurfaceInputPolicy::Disabled;
}

bool UiRuntime::IsPointerOver(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr && (slot->PointerInside || slot->HeldButtons != 0);
}

void UiRuntime::SetSurfaceDestination(UiSurfaceId surface, UiSurfaceDestination destination)
{
    if (Surface* slot = ResolveSurface(surface))
    {
        slot->Destination = destination;
        RebuildInputOrder();
    }
}

UiSurfaceDestination UiRuntime::GetSurfaceDestination(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr ? slot->Destination : UiSurfaceDestination::Window;
}

void UiRuntime::SetSurfaceWindow(UiSurfaceId surface, std::uint32_t platformWindowId)
{
    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr)
        return;
    const std::uint32_t window = platformWindowId == PrimaryWindowId ? 0 : platformWindowId;
    if (slot->WindowId == window)
        return;
    // Whatever the old window's pointer and focus were doing is not the new
    // window's business.
    if (slot->Context != nullptr)
    {
        EndPress(*slot);
        (void)slot->Context->ProcessMouseLeave();
        DropFocus(*slot->Context);
    }
    slot->PointerInside = false;
    slot->WindowId = window;
}

std::uint32_t UiRuntime::GetSurfaceWindow(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr ? slot->WindowId : 0;
}

void UiRuntime::SetSurfaceLayer(UiSurfaceId surface, std::uint8_t layer)
{
    if (Surface* slot = ResolveSurface(surface))
        slot->Layer = layer;
}

std::uint8_t UiRuntime::GetSurfaceLayer(UiSurfaceId surface) const
{
    const Surface* slot = ResolveSurface(surface);
    return slot != nullptr ? slot->Layer : 0;
}

bool UiRuntime::Hears(const Surface& surface, std::uint32_t eventWindow) const
{
    if (eventWindow == 0)
        return surface.WindowId == 0;
    const std::uint32_t own = surface.WindowId != 0 ? surface.WindowId : PrimaryWindowId;
    return eventWindow == own;
}

SDL_Window* UiRuntime::WindowOf(const Surface& surface) const
{
    return surface.WindowId != 0 ? SDL_GetWindowFromID(surface.WindowId) : PrimaryWindow;
}

const UiDrawFrame* UiRuntime::OffscreenFrame(UiSurfaceId surface) const
{
    for (const auto& [id, frame] : OffscreenFrames)
        if (id == surface)
            return &frame;
    return nullptr;
}

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
            Refuse(UiDiagnosticKind::ResourceUnresolved, std::string(resource.Path), std::format("UiRuntime: package '{}' names {} '{}', which did not load",
                      packagePath, AssetTypeToString(resource.Type), resource.Path));
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

UiScreenHandle UiRuntime::OpenScreen(UiSurfaceId surface, const UiScreenDesc& desc)
{
    if (!Ready)
        return {};

    const std::string_view packagePath = desc.PackagePath;
    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr)
    {
        Log.Error("UiRuntime: cannot open '{}' on a surface that does not exist", packagePath);
        return {};
    }

    // Everything reported while this opens names the surface; the screen joins
    // the attribution the moment its handle is decided, below.
    UiDiagnosticLog::Scope opening(Diagnostics, { {}, surface });

    AssetLease packageLease = Assets.LoadLease(packagePath, AssetType::UiPackage);
    if (!packageLease.IsValid())
    {
        Refuse(UiDiagnosticKind::PackageUnavailable, std::string(packagePath), std::format("UiRuntime: UI package '{}' did not load", packagePath));
        return {};
    }

    const UiPackage* package = Packages.Get(UiPackageHandle::FromToken(packageLease.OpaqueToken()));
    if (package == nullptr)
    {
        Refuse(UiDiagnosticKind::PackageUnavailable, std::string(packagePath), std::format("UiRuntime: UI package '{}' loaded but is not resident", packagePath));
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

    pending.Modal = desc.Modal;
    pending.PackagePath = std::string(packagePath);
    pending.Description = desc;
    pending.ModelName = desc.ModelName;
    pending.ActionNames = desc.Actions;
    pending.RowLists.reserve(desc.RowLists.size());
    for (const std::string& path : desc.RowLists)
    {
        auto rows = std::make_unique<Screen::BoundRows>();
        rows->Path = path;
        pending.RowLists.push_back(std::move(rows));
    }
    pending.Arrays.reserve(desc.Arrays.size());
    for (const std::string& path : desc.Arrays)
    {
        auto array = std::make_unique<Screen::BoundArray>();
        array->Path = path;
        pending.Arrays.push_back(std::move(array));
    }
    pending.Properties.reserve(desc.Properties.size());
    for (const UiModelProperty& property : desc.Properties)
        pending.Properties.push_back(
            BoundProperty{ property.Path, property.Initial, property.Editable });

    // The screen takes its slot before the model binds and before the document
    // loads, and everything after this point works against the slot rather than
    // against a local.
    //
    // It has to. A model binding captures the screen it reads from, and a
    // capture of a local that is later moved into the slot is a dangling
    // pointer the moment the document evaluates it. An action raised during the
    // load also needs the handle the host will match against, which means that
    // handle must already be decided.
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

    const UiScreenHandle handle{ static_cast<std::uint32_t>(index + 1), generation };
    opening.Set({ handle, surface });

    // What the cooker noticed about this package, hung on the screen that is
    // about to show it. Reported here rather than when the package became
    // resident, because this is the point that has a screen to attribute it to
    // and a consumer that may be looking.
    for (const UiUnsupportedFeature& note : package->Unsupported)
    {
        UiDiagnostic entry;
        entry.Severity = UiDiagnosticSeverity::Warning;
        entry.Source = UiDiagnosticSource::Cook;
        entry.Kind = UiDiagnosticKind::UnsupportedStyle;
        entry.Path = note.SourcePath;
        entry.Line = note.Line;
        entry.Message = std::format(
            "UiPackage '{}': '{}' at {}:{} is outside the supported rendering profile",
            packagePath, note.Feature, note.SourcePath, note.Line);
        Diagnostics.Push(std::move(entry));
    }

    if (!desc.ModelName.empty() && !BuildModel(screen, handle, *slot))
    {
        CloseScreenSlot(screen);
        return {};
    }

    Rml::ElementDocument* document = nullptr;
    {
        // The file interface answers from this package, and the resolver from
        // this screen, for exactly as long as the load runs -- and from nothing
        // at all outside it.
        const RmlPackageFileSource::ActivePackageScope scope(*FileSource, *package);
        const ActiveScreenScope active(*this, screen, handle);
        document = slot->Context->LoadDocument(package->RootDocumentName);
    }

    if (document == nullptr)
    {
        Refuse(UiDiagnosticKind::DocumentInvalid, std::string(packagePath), std::format("UiRuntime: '{}' did not parse into a document", packagePath));
        CloseScreenSlot(screen);
        return {};
    }

    // Modal takes focus from the documents under it, which is the whole of what
    // "modal" means here.
    document->Show(screen.Modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None);

    screen.Document = document;
    screen.Package = std::move(packageLease);
    screen.Resources = std::move(resourceLeases);
    screen.PackageVersion = Packages.GetReloadVersion(
        UiPackageHandle::FromToken(screen.Package.OpaqueToken()));
    // Built against whatever the host has supplied by now, so it is not
    // restyled on the frame it opened.
    screen.StyleVersion = StyleVersion;
    screen.Live = true;

    return handle;
}

void UiRuntime::CloseScreenSlot(Screen& screen)
{
    // Anything this screen asked for that nobody has drained yet goes with it.
    // The handle stops resolving the moment the generation below bumps, so a
    // surviving action could never be matched by DrainActions(screen) again --
    // it would sit in the queue until some other controller took everything.
    // Dropping it is also the honest answer: it names a screen that is gone.
    //
    // Done before the generation bump, while the handle still matches.
    if (!PendingActions.empty())
    {
        const auto index = static_cast<std::uint32_t>(&screen - Screens.data());
        const UiScreenHandle closing{ index + 1, screen.Generation };
        std::erase_if(PendingActions, [closing](const UiAction& action) {
            return action.Screen == closing;
        });
    }

    // References handed out for this document die with it.
    screen.Elements.clear();

    if (screen.Document != nullptr)
    {
        // Close() unloads it from its context. The context owns the document, so
        // nothing here deletes it.
        screen.Document->Close();
        screen.Document = nullptr;
    }

    // After the document, so a resource is released only once nothing is laid
    // out against it.
    // The model outlives the document only long enough to be removed: its
    // bindings capture this screen's slot, and a context that still holds them
    // after the slot is reused would feed the next screen's values into a
    // document that is gone.
    if (screen.Model != nullptr)
    {
        if (Surface* slot = ResolveSurface(screen.Surface);
            slot != nullptr && slot->Context != nullptr)
        {
            slot->Context->RemoveDataModel(screen.ModelName);
        }
        screen.Model.reset();
    }
    screen.Properties.clear();
    screen.Arrays.clear();
    screen.RowLists.clear();
    screen.ActionNames.clear();
    screen.Description = {};
    screen.PackagePath.clear();
    screen.PackageVersion = 0;
    screen.StyleVersion = 0;
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

void UiRuntime::ForgetFontResources()
{
    // Blunt, and deliberately so. The engine keys a loaded face by family, not
    // by the asset it came from, so there is no way to drop one face without
    // dropping the set -- and a rebuild immediately re-requests every face the
    // documents declare, through the file interface, which answers from the
    // leases the screens hold. The cost is re-parsing faces during an authoring
    // edit, which is the cheapest moment to pay it.
    Rml::ReleaseFontResources();
}

void UiRuntime::ReloadChangedScreens()
{
    for (std::size_t i = 0; i < Screens.size(); ++i)
    {
        Screen& screen = Screens[i];
        if (!screen.Live || !screen.Package.IsValid())
            continue;

        const auto handle = UiPackageHandle::FromToken(screen.Package.OpaqueToken());
        const std::uint64_t version = Packages.GetReloadVersion(handle);
        if (version == screen.PackageVersion)
            continue;

        // Stamped before the attempt, not after. A package that fails to build
        // must not be retried every frame for as long as it stays broken -- the
        // author is looking at the error, and a log filling at frame rate is
        // not help.
        screen.PackageVersion = version;
        // A rebuild re-reads every sheet, host copies included, so it is
        // already current by the time RestyleChangedScreens looks.
        screen.StyleVersion = StyleVersion;

        const UiScreenHandle screenHandle{ static_cast<std::uint32_t>(i + 1),
                                           screen.Generation };
        if (!RebuildScreen(screen, screenHandle))
        {
            UiDiagnostic entry;
            entry.Severity = UiDiagnosticSeverity::Error;
            entry.Source = UiDiagnosticSource::Runtime;
            entry.Kind = UiDiagnosticKind::RebuildFailed;
            entry.Screen = screenHandle;
            entry.Surface = screen.Surface;
            entry.Path = screen.PackagePath;
            entry.Message = std::format("UiRuntime: '{}' failed to rebuild after an edit; "
                                        "the document already open is left alone",
                                        screen.PackagePath);
            Diagnostics.Push(std::move(entry));
        }
    }
}

bool UiRuntime::RebuildScreen(Screen& screen, UiScreenHandle handle)
{
    const UiDiagnosticLog::Scope rebuilding(Diagnostics, { handle, screen.Surface });
    Surface* slot = ResolveSurface(screen.Surface);
    if (slot == nullptr || slot->Context == nullptr)
        return false;

    const UiPackage* package =
        Packages.Get(UiPackageHandle::FromToken(screen.Package.OpaqueToken()));
    if (package == nullptr || !package->IsValid())
        return false;

    // What the host published, carried across. A reload is an author changing
    // the markup, not the application forgetting what it was presenting -- a
    // HUD that blanked its health every time somebody tweaked a stylesheet
    // would make the feedback loop useless.
    std::vector<UiValue> published;
    published.reserve(screen.Properties.size());
    for (const BoundProperty& property : screen.Properties)
        published.push_back(property.Value);

    std::vector<std::vector<std::string>> lists;
    lists.reserve(screen.Arrays.size());
    for (const std::unique_ptr<Screen::BoundArray>& array : screen.Arrays)
        lists.push_back(array->Items);

    std::vector<std::vector<UiRow>> rowLists;
    rowLists.reserve(screen.RowLists.size());
    for (const std::unique_ptr<Screen::BoundRows>& rows : screen.RowLists)
        rowLists.push_back(rows->Items);

    // The document engine caches parsed stylesheets and templates by name, so a
    // rebuild would re-use the sheet it parsed the first time and the edit would
    // never appear. Cleared here rather than at reload time because this is the
    // only place that is about to re-parse.
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
    ForgetFontResources();

    // The old document goes before the new one is built: two documents from one
    // package, alive at once on the same context, would both be showing. Every
    // element reference handed out for it goes with it.
    screen.Elements.clear();
    if (screen.Document != nullptr)
    {
        screen.Document->Close();
        screen.Document = nullptr;
    }
    if (screen.Model != nullptr)
    {
        slot->Context->RemoveDataModel(screen.ModelName);
        screen.Model.reset();
    }

    // Rebuilt from the description the host gave, so its ids keep meaning what
    // they meant: a property the host resolved once at open must still be that
    // property afterwards.
    screen.Properties.clear();
    for (const UiModelProperty& property : screen.Description.Properties)
    {
        screen.Properties.push_back(
            BoundProperty{ property.Path, property.Initial, property.Editable });
    }
    screen.Arrays.clear();
    for (const std::string& path : screen.Description.Arrays)
    {
        auto array = std::make_unique<Screen::BoundArray>();
        array->Path = path;
        screen.Arrays.push_back(std::move(array));
    }
    screen.RowLists.clear();
    for (const std::string& path : screen.Description.RowLists)
    {
        auto rows = std::make_unique<Screen::BoundRows>();
        rows->Path = path;
        screen.RowLists.push_back(std::move(rows));
    }

    for (std::size_t i = 0; i < screen.Properties.size() && i < published.size(); ++i)
        screen.Properties[i].Value = published[i];
    for (std::size_t i = 0; i < screen.Arrays.size() && i < lists.size(); ++i)
        screen.Arrays[i]->Items = lists[i];
    for (std::size_t i = 0; i < screen.RowLists.size() && i < rowLists.size(); ++i)
        screen.RowLists[i]->Items = rowLists[i];

    if (!screen.ModelName.empty() && !BuildModel(screen, handle, *slot))
        return false;

    Rml::ElementDocument* document = nullptr;
    {
        const RmlPackageFileSource::ActivePackageScope scope(*FileSource, *package);
        const ActiveScreenScope active(*this, screen, handle);
        document = slot->Context->LoadDocument(package->RootDocumentName);
    }
    if (document == nullptr)
        return false;

    document->Show(screen.Modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None);
    screen.Document = document;
    return true;
}

bool UiRuntime::SetHostStyleSheet(std::string_view name, std::string_view text)
{
    if (!Ready || name.empty())
        return false;

    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    if (!FileSource->ReplaceHostFile(name, std::move(bytes)))
        return false;

    // The document engine caches a parsed stylesheet by name, so without this
    // the next screen to open would be handed the parse of the bytes that were
    // just replaced. Screens already open re-parse through ReloadStyleSheet,
    // which clears the cache itself.
    Rml::Factory::ClearStyleSheetCache();

    // One counter for the whole set rather than one per sheet. A theme change
    // is rare and a document does not say which sheets it read, so working out
    // who was affected would cost more bookkeeping than restyling everything.
    ++StyleVersion;
    return true;
}

void UiRuntime::RestyleChangedScreens()
{
    for (Screen& screen : Screens)
    {
        if (!screen.Live || screen.Document == nullptr || screen.StyleVersion == StyleVersion)
            continue;
        screen.StyleVersion = StyleVersion;

        const UiPackage* package =
            Packages.Get(UiPackageHandle::FromToken(screen.Package.OpaqueToken()));
        if (package == nullptr || !package->IsValid())
            continue;

        // The document engine re-reads its own source to recover the stylesheet
        // graph, and every one of those reads comes back through the package
        // file source -- which answers nothing without a package bound. The
        // screen goes with it: re-parsing a sheet re-runs its @font-face rules,
        // and a face is resolved from the leases the screen holds.
        // Fonts are deliberately left alone. ReleaseFontResources dirties every
        // element and updates every context to re-rasterize glyphs, which is
        // the right answer when a face's bytes changed and pointless work with
        // a re-entrant context update in the middle of this one when a theme
        // only changed colours. Faces the documents declared are already
        // loaded, and re-parsing an @font-face asks for one already there.
        RmlPackageFileSource::ActivePackageScope scope(*FileSource, *package);
        const ActiveScreenScope active(*this, screen, HandleOf(screen));
        screen.Document->ReloadStyleSheet();
    }
}

void UiRuntime::Update()
{
    if (!Ready)
        return;

    // Before layout, so an edit lands in the frame the reload committed rather
    // than the one after it.
    ReloadChangedScreens();
    // After, so a document rebuilt this frame is not immediately restyled: a
    // rebuild already read the current sheets.
    RestyleChangedScreens();

    for (std::size_t index = 0; index < Surfaces.size(); ++index)
    {
        Surface& surface = Surfaces[index];
        if (!surface.Live || surface.Context == nullptr)
            continue;

        const UiDiagnosticLog::Scope updating(Diagnostics, SurfaceAttribution(index));
        surface.Context->Update();
    }
    for (Screen& screen : Screens)
        for (const std::unique_ptr<Screen::BoundRows>& rows : screen.RowLists)
            rows->Departing.clear();
}

void UiRuntime::ExtractRender()
{
    DrawFrames.clear();
    OffscreenFrames.clear();
    ExtractedTextureChanges = {};
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
        ActiveRenderSurface = surfaceId;

        Recorder->BeginFrame(surface.Size);
        surface.Context->Render();
        ActiveRenderSurface = {};

        UiDrawFrame frame = Recorder->EndFrame();
        if (frame.IsEmpty())
            continue;
        frame.WindowId = surface.WindowId;
        frame.Layer = surface.Layer;
        // Published once, to one place. The window feature reads the list; a
        // host feature drawing this surface into its own target reads the
        // entry by surface, and never sees the same recording twice.
        if (surface.Destination == UiSurfaceDestination::Offscreen)
            OffscreenFrames.emplace_back(surfaceId, std::move(frame));
        else
            DrawFrames.push_back(std::move(frame));
    }
    ExtractedTextureChanges = Recorder->TakeTextureChanges();
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

std::vector<const UiRuntime::Screen*> UiRuntime::ResolutionCandidates() const
{
    // A document being loaded, rebuilt or restyled resolves against its own
    // package and nothing else: that is what makes an undeclared source an
    // authoring error rather than a lucky hit on a neighbour's table.
    if (ActiveScreen != nullptr)
        return { ActiveScreen };

    if (!ActiveRenderSurface.IsValid())
        return {};

    std::vector<const Screen*> candidates;
    for (const Screen& screen : Screens)
    {
        if (screen.Live && screen.Surface == ActiveRenderSurface && screen.Document != nullptr)
            candidates.push_back(&screen);
    }
    return candidates;
}

UiHostImageId UiRuntime::SetHostImage(std::string_view name, RenderExtent size)
{
    HostImage& image = HostImages[std::string(name)];
    if (image.Id == kInvalidUiHostImage)
        image.Id = NextHostImageId++;
    image.Size = size;
    return image.Id;
}

void UiRuntime::RemoveHostImage(std::string_view name)
{
    HostImages.erase(std::string(name));
}

bool UiRuntime::ResolveHostImage(std::string_view name, UiHostImageId& outId, RenderExtent& outSize)
{
    const auto it = HostImages.find(std::string(name));
    if (it == HostImages.end())
        return false;
    outId = it->second.Id;
    outSize = it->second.Size;
    return true;
}

bool UiRuntime::ResolveTexture(std::string_view source,
                               TextureHandle& outHandle,
                               RenderExtent& outSize)
{
    if (Textures == nullptr)
        return false;

    // Each candidate resolves the source against its own resource root before
    // looking it up, because that root is what the cooker resolved against.
    for (const Screen* screen : ResolutionCandidates())
    {
        const std::string assetPath = AssetPathFor(*screen, source);
        const auto it = screen->TexturesByAssetPath.find(assetPath);
        if (it == screen->TexturesByAssetPath.end())
            continue;

        outHandle = it->second;
        outSize = Textures->GetExtent(it->second);
        return true;
    }
    return false;
}

bool UiRuntime::ResolveResourceBytes(std::string_view source,
                                     const std::vector<std::byte>*& outBytes)
{
    const FontFace* face = nullptr;
    for (const Screen* screen : ResolutionCandidates())
    {
        const std::string assetPath = AssetPathFor(*screen, source);
        const auto it = screen->FontsByAssetPath.find(assetPath);
        if (it == screen->FontsByAssetPath.end())
            continue;
        face = Fonts.Get(it->second);
        if (face != nullptr)
            break;
    }
    if (face == nullptr)
        return false;

    // The face bytes out of the cooked container, never a path: a shipped build
    // has no font file on disk to find.
    outBytes = &face->Bytes;
    return true;
}

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
