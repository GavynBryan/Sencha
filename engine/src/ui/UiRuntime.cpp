#include "UiRuntime.h"

#include "rml/RmlInputBridge.h"
#include "rml/RmlRenderRecorder.h"
#include "rml/RmlPackageFileSource.h"
#include "rml/RmlSystemBridge.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Factory.h>
#include <SDL3/SDL_events.h>
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
#include <utility>

namespace
{
// The document engine's interfaces are process-global, so two runtimes in one
// process would silently fight over them. Refusing the second is a clearer
// failure than the corruption that follows from allowing it.
std::atomic<bool> g_RuntimeLive{ false };

// The document engine's variant is what a binding reads and writes; UiValue is
// what crosses the public boundary. Converting at exactly these two points is
// what stops the engine's type appearing anywhere a host can see.
void ToRmlVariant(const UiValue& value, Rml::Variant& out)
{
    switch (value.Kind())
    {
    case UiValueKind::Bool:   out = Rml::Variant(value.AsBool()); break;
    case UiValueKind::Int:    out = Rml::Variant(static_cast<int>(value.AsInt())); break;
    case UiValueKind::Float:  out = Rml::Variant(static_cast<float>(value.AsFloat())); break;
    case UiValueKind::String: out = Rml::Variant(Rml::String(value.AsString())); break;
    // Presented as text: an identity is for a document to pass back, not to do
    // arithmetic on, and every id fits a string exactly where a float would
    // start losing digits above 2^53.
    case UiValueKind::Id:     out = Rml::Variant(Rml::ToString(value.AsId())); break;
    case UiValueKind::None:
    default:                  out = Rml::Variant(); break;
    }
}

UiValue FromRmlVariant(const Rml::Variant& value)
{
    switch (value.GetType())
    {
    case Rml::Variant::BOOL:   return UiValue(value.Get<bool>());
    case Rml::Variant::CHAR:
    case Rml::Variant::INT:    return UiValue(static_cast<std::int64_t>(value.Get<int>()));
    case Rml::Variant::INT64:  return UiValue(value.Get<std::int64_t>());
    case Rml::Variant::FLOAT:  return UiValue(static_cast<double>(value.Get<float>()));
    case Rml::Variant::DOUBLE: return UiValue(value.Get<double>());
    case Rml::Variant::STRING: return UiValue(value.Get<Rml::String>());
    default:                   return UiValue();
    }
}

// A data-model name the document engine can actually bind.
//
// Names are identifiers because a data expression reads a dot as member access:
// "pause.quit" is not a callback called "pause.quit", it is the member "quit"
// of something called "pause". Binding one silently fails, and the first sign
// is a button that does nothing, so it is refused here with the reason instead.
bool IsBindableName(std::string_view name)
{
    if (name.empty())
        return false;
    if (std::isdigit(static_cast<unsigned char>(name.front())) != 0)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

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
    TextInput = std::make_unique<RmlTextInputBridge>(window);
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

bool UiRuntime::BuildModel(Screen& screen, UiScreenHandle handle, Surface& surface)
{
    Rml::DataModelConstructor constructor = surface.Context->CreateDataModel(screen.ModelName);
    if (!constructor)
    {
        Log.Error("UiRuntime: could not create data model '{}'", screen.ModelName);
        return false;
    }

    // The model is registered on the context the moment it is created, so every
    // failure below has to take it back off. Leaving it there would make the
    // next screen declaring the same model name fail to create one -- a
    // refused open poisoning the next attempt, which is far harder to read than
    // the original error.
    const auto fail = [&] {
        surface.Context->RemoveDataModel(screen.ModelName);
        return false;
    };

    for (std::size_t i = 0; i < screen.Properties.size(); ++i)
    {
        if (!IsBindableName(screen.Properties[i].Path))
        {
            Log.Error("UiRuntime: '{}' is not a bindable property name. A data expression "
                      "reads '.' as member access, so a name needs to be an identifier "
                      "(letters, digits, underscore, not starting with a digit)",
                      screen.Properties[i].Path);
            return fail();
        }

        // Bound by function rather than by address. A pointer binding would
        // need one C++ variable per declared type and stable storage for every
        // one of them; a getter routes every kind through the same path and
        // keeps the value where the dirty comparison already lives.
        //
        // The capture is the screen slot and an index, not a pointer into the
        // property vector: the vector is rebuilt when a slot is reused, and a
        // captured pointer would survive that.
        Screen* slot = &screen;
        const std::size_t index = i;

        // A setter only where the description said a control may write. Without
        // one the binding is read-only and the document engine refuses the
        // write, rather than accepting it into a value nothing will ever read
        // back.
        Rml::DataSetFunc setter;
        if (screen.Properties[i].Editable)
        {
            setter = [slot, index](const Rml::Variant& in) {
                // Presentation only. Nothing here reaches the application: the
                // host reads this back when the document says to apply.
                slot->Properties[index].Value = FromRmlVariant(in);
            };
        }

        if (!constructor.BindFunc(
                screen.Properties[i].Path,
                [slot, index](Rml::Variant& out) { ToRmlVariant(slot->Properties[index].Value, out); },
                std::move(setter)))
        {
            Log.Error("UiRuntime: data model '{}' refused the property '{}'",
                      screen.ModelName, screen.Properties[i].Path);
            return fail();
        }
    }

    // Registered once per model, before any array is bound: the engine resolves
    // the element type when the container type is registered, not when a
    // variable using it is.
    if (!screen.Arrays.empty())
        (void)constructor.RegisterArray<std::vector<std::string>>();

    if (!screen.RowLists.empty())
    {
        // Members bound by pointer-to-member, which makes each one read-write.
        // That is what lets a control inside a repeated row edit its value
        // without the model needing a setter per row -- and the value still
        // goes no further than the presentation copy.
        if (auto row = constructor.RegisterStruct<UiRow>())
        {
            (void)row.RegisterMember("label", &UiRow::Label);
            (void)row.RegisterMember("value", &UiRow::Value);
            (void)row.RegisterMember("detail", &UiRow::Detail);
            (void)row.RegisterMember("editable", &UiRow::Editable);
        }
        (void)constructor.RegisterArray<std::vector<UiRow>>();
    }

    for (const std::unique_ptr<Screen::BoundRows>& rows : screen.RowLists)
    {
        if (!IsBindableName(rows->Path))
        {
            Log.Error("UiRuntime: '{}' is not a bindable row-list name", rows->Path);
            return fail();
        }
        if (!constructor.Bind(rows->Path, &rows->Items))
        {
            Log.Error("UiRuntime: data model '{}' refused the row list '{}'",
                      screen.ModelName, rows->Path);
            return fail();
        }
    }

    for (const std::unique_ptr<Screen::BoundArray>& array : screen.Arrays)
    {
        if (!IsBindableName(array->Path))
        {
            Log.Error("UiRuntime: '{}' is not a bindable list name, for the same reason "
                      "a property is not: a data expression reads '.' as member access",
                      array->Path);
            return fail();
        }
        if (!constructor.Bind(array->Path, &array->Items))
        {
            Log.Error("UiRuntime: data model '{}' refused the list '{}'",
                      screen.ModelName, array->Path);
            return fail();
        }
    }

    for (std::size_t i = 0; i < screen.ActionNames.size(); ++i)
    {
        if (!IsBindableName(screen.ActionNames[i]))
        {
            Log.Error("UiRuntime: '{}' is not a bindable action name, for the same reason "
                      "a property is not: a data expression reads '.' as member access",
                      screen.ActionNames[i]);
            return fail();
        }

        const UiActionId id = UiActionIdAt(i);
        if (!constructor.BindEventCallback(
                screen.ActionNames[i],
                [this, handle, id](Rml::DataModelHandle, Rml::Event&,
                                   const Rml::VariantList& arguments) {
                    UiAction action;
                    action.Screen = handle;
                    action.Id = id;
                    action.Arguments.reserve(arguments.size());
                    for (const Rml::Variant& argument : arguments)
                        action.Arguments.push_back(FromRmlVariant(argument));
                    PendingActions.push_back(std::move(action));
                }))
        {
            Log.Error("UiRuntime: data model '{}' refused the action '{}'",
                      screen.ModelName, screen.ActionNames[i]);
            return fail();
        }
    }

    screen.Model = std::make_unique<Rml::DataModelHandle>(constructor.GetModelHandle());
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
        ActiveScreen = &screen;
        document = slot->Context->LoadDocument(package->RootDocumentName);
        ActiveScreen = nullptr;
    }

    if (document == nullptr)
    {
        Log.Error("UiRuntime: '{}' did not parse into a document", packagePath);
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

// -- frame -------------------------------------------------------------------

bool UiRuntime::SetValue(UiScreenHandle screen, UiModelPropertyId property, UiValue value)
{
    Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !property.IsValid() || property.Value > slot->Properties.size())
        return false;

    BoundProperty& bound = slot->Properties[property.Value - 1];
    // Compare before marking dirty. A model that dirties everything every frame
    // re-evaluates every binding reading it, which for a HUD publishing one
    // unchanged number is the whole cost of having a HUD.
    if (bound.Value == value)
        return false;

    bound.Value = std::move(value);
    if (slot->Model != nullptr)
        slot->Model->DirtyVariable(bound.Path);
    return true;
}

bool UiRuntime::SetArray(UiScreenHandle screen, UiModelArrayId array,
                         std::span<const std::string> items)
{
    Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !array.IsValid() || array.Value > slot->Arrays.size())
        return false;

    Screen::BoundArray& bound = *slot->Arrays[array.Value - 1];
    // Same compare-then-dirty rule as a value. A list republished unchanged --
    // which is what a panel does every frame -- must not re-run every binding
    // that repeats over it.
    if (bound.Items.size() == items.size()
        && std::equal(bound.Items.begin(), bound.Items.end(), items.begin()))
    {
        return false;
    }

    bound.Items.assign(items.begin(), items.end());
    if (slot->Model != nullptr)
        slot->Model->DirtyVariable(bound.Path);
    return true;
}

bool UiRuntime::SetRows(UiScreenHandle screen, UiModelRowsId rows,
                        std::span<const UiRow> items)
{
    Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !rows.IsValid() || rows.Value > slot->RowLists.size())
        return false;

    Screen::BoundRows& bound = *slot->RowLists[rows.Value - 1];
    if (bound.Items.size() == items.size()
        && std::equal(bound.Items.begin(), bound.Items.end(), items.begin()))
    {
        return false;
    }

    bound.Items.assign(items.begin(), items.end());
    if (slot->Model != nullptr)
        slot->Model->DirtyVariable(bound.Path);
    return true;
}

std::vector<UiRow> UiRuntime::GetRows(UiScreenHandle screen, UiModelRowsId rows) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !rows.IsValid() || rows.Value > slot->RowLists.size())
        return {};
    return slot->RowLists[rows.Value - 1]->Items;
}

UiModelRowsId UiRuntime::FindRows(UiScreenHandle screen, std::string_view path) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return {};
    for (std::size_t i = 0; i < slot->RowLists.size(); ++i)
    {
        if (slot->RowLists[i]->Path == path)
            return UiRowsIdAt(i);
    }
    return {};
}

std::size_t UiRuntime::ArraySize(UiScreenHandle screen, UiModelArrayId array) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !array.IsValid() || array.Value > slot->Arrays.size())
        return 0;
    return slot->Arrays[array.Value - 1]->Items.size();
}

UiModelArrayId UiRuntime::FindArray(UiScreenHandle screen, std::string_view path) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return {};
    for (std::size_t i = 0; i < slot->Arrays.size(); ++i)
    {
        if (slot->Arrays[i]->Path == path)
            return UiArrayIdAt(i);
    }
    return {};
}

UiValue UiRuntime::GetValue(UiScreenHandle screen, UiModelPropertyId property) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !property.IsValid() || property.Value > slot->Properties.size())
        return {};
    return slot->Properties[property.Value - 1].Value;
}

UiModelPropertyId UiRuntime::FindProperty(UiScreenHandle screen, std::string_view path) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return {};
    for (std::size_t i = 0; i < slot->Properties.size(); ++i)
    {
        if (slot->Properties[i].Path == path)
            return UiPropertyIdAt(i);
    }
    return {};
}

UiActionId UiRuntime::FindAction(UiScreenHandle screen, std::string_view name) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return {};
    for (std::size_t i = 0; i < slot->ActionNames.size(); ++i)
    {
        if (slot->ActionNames[i] == name)
            return UiActionIdAt(i);
    }
    return {};
}

std::vector<UiAction> UiRuntime::DrainActions()
{
    return std::exchange(PendingActions, {});
}

std::vector<UiAction> UiRuntime::DrainActions(UiScreenHandle screen)
{
    std::vector<UiAction> taken;
    std::vector<UiAction> left;
    left.reserve(PendingActions.size());
    for (UiAction& action : PendingActions)
    {
        if (action.Screen == screen)
            taken.push_back(std::move(action));
        else
            left.push_back(std::move(action));
    }
    // Order within each screen survives, which is what matters: a begin and the
    // commit that follows it must not arrive the other way round.
    PendingActions = std::move(left);
    return taken;
}

bool UiRuntime::ProcessPlatformEvent(const SDL_Event& event)
{
    if (!Ready)
        return false;

    // Topmost first, and the first surface to consume ends it. Surfaces are
    // created bottom-up, so reverse creation order is the z-order a host would
    // expect without having to state one.
    for (auto it = Surfaces.rbegin(); it != Surfaces.rend(); ++it)
    {
        Surface& surface = *it;
        if (!surface.Live || surface.Context == nullptr)
            continue;

        Rml::Context& context = *surface.Context;
        const int modifiers = ToRmlKeyModifiers(SDL_GetModState());

        // Every Process* here returns true when the UI is NOT interacting, so
        // consumption is the negation. Reading it the other way round would
        // hand every event to the UI and none to the game.
        switch (event.type)
        {
        case SDL_EVENT_MOUSE_MOTION:
            PointerX = static_cast<int>(event.motion.x);
            PointerY = static_cast<int>(event.motion.y);
            PointerInside = true;
            if (!context.ProcessMouseMove(PointerX, PointerY, modifiers))
                return true;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const int button = ToRmlMouseButton(event.button.button);
            if (button < 0)
                break;
            // Re-stated, because a press with no preceding move -- a synthetic
            // event, a tap -- would otherwise be resolved against wherever the
            // pointer happened to be last.
            PointerX = static_cast<int>(event.button.x);
            PointerY = static_cast<int>(event.button.y);
            PointerInside = true;
            (void)context.ProcessMouseMove(PointerX, PointerY, modifiers);
            const bool free = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                ? context.ProcessMouseButtonDown(button, modifiers)
                : context.ProcessMouseButtonUp(button, modifiers);
            if (!free)
                return true;
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
            if (!context.ProcessMouseWheel(
                    Rml::Vector2f(-event.wheel.x, -event.wheel.y), modifiers))
            {
                return true;
            }
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const Rml::Input::KeyIdentifier key = ToRmlKey(event.key.scancode);
            if (key == Rml::Input::KI_UNKNOWN)
                break;
            const bool free = event.type == SDL_EVENT_KEY_DOWN
                ? context.ProcessKeyDown(key, modifiers)
                : context.ProcessKeyUp(key, modifiers);
            // Only claimed while a field is actually taking text. A menu that
            // swallowed every key would take the console and the pause key with
            // it, and neither belongs to the menu.
            if (!free || TextInput->IsTextInputActive())
                return true;
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
            // Composed characters, from the platform. Never reassembled from
            // keycodes, which is wrong in every locale but the author's.
            if (TextInput->IsTextInputActive())
            {
                (void)context.ProcessTextInput(Rml::String(event.text.text));
                return true;
            }
            break;

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            PointerInside = false;
            (void)context.ProcessMouseLeave();
            break;

        default:
            break;
        }
    }

    return false;
}

UiInputCapture UiRuntime::Capture() const
{
    UiInputCapture capture;
    if (!Ready)
        return capture;

    for (const Surface& surface : Surfaces)
    {
        if (!surface.Live || surface.Context == nullptr)
            continue;
        if (surface.Context->IsMouseInteracting())
            capture.Mouse = true;
    }

    // Keyboard capture is a text field having focus, and nothing wider. A
    // screen merely being open does not take the keyboard: a HUD is open for
    // the whole game, and a pause menu still has to let the console key
    // through. What suppresses gameplay controls is an InputContextLease the
    // host takes, which is a decision this layer does not get to make.
    capture.Keyboard = TextInput != nullptr && TextInput->IsTextInputActive();
    return capture;
}

void UiRuntime::Navigate(UiSurfaceId surface, UiNavigation direction)
{
    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr || slot->Context == nullptr)
        return;

    // Expressed as the keys the document engine's own focus and spatial
    // navigation already understand. What decided to send it -- a stick, a
    // remapped button, a key -- was settled by the host's action mapping before
    // this was called, which is the whole point of not reading devices here.
    Rml::Input::KeyIdentifier key = Rml::Input::KI_UNKNOWN;
    int modifiers = 0;
    switch (direction)
    {
    case UiNavigation::Up:       key = Rml::Input::KI_UP; break;
    case UiNavigation::Down:     key = Rml::Input::KI_DOWN; break;
    case UiNavigation::Left:     key = Rml::Input::KI_LEFT; break;
    case UiNavigation::Right:    key = Rml::Input::KI_RIGHT; break;
    case UiNavigation::Next:     key = Rml::Input::KI_TAB; break;
    case UiNavigation::Previous: key = Rml::Input::KI_TAB;
                                 modifiers = Rml::Input::KM_SHIFT; break;
    case UiNavigation::Accept:   key = Rml::Input::KI_RETURN; break;
    case UiNavigation::Cancel:   key = Rml::Input::KI_ESCAPE; break;
    }
    if (key == Rml::Input::KI_UNKNOWN)
        return;

    (void)slot->Context->ProcessKeyDown(key, modifiers);
    (void)slot->Context->ProcessKeyUp(key, modifiers);
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
            Log.Error("UiRuntime: '{}' failed to rebuild after an edit; "
                      "the document already open is left alone", screen.PackagePath);
        }
    }
}

bool UiRuntime::RebuildScreen(Screen& screen, UiScreenHandle handle)
{
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
    // package, alive at once on the same context, would both be showing.
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
        ActiveScreen = &screen;
        document = slot->Context->LoadDocument(package->RootDocumentName);
        ActiveScreen = nullptr;
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
        ActiveScreen = &screen;
        screen.Document->ReloadStyleSheet();
        ActiveScreen = nullptr;
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
