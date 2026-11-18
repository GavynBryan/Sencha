#include <assets/runtime/ContentVocabulary.h>

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/CuePresentationLibrary.h>
#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/RuntimeAssets.h>
#include <attributes/AttributeDeclarations.h>
#include <attributes/AttributeRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/assets/AssetRegistry.h>
#include <ecs/World.h>
#include <effects/EffectDefinitionData.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagDeclarations.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <equipment/Equipment.h>
#include <equipment/EquipmentLayoutData.h>
#include <equipment/WornGrantEntries.h>
#include <inventory/ItemDefinitionData.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <string_view>

namespace
{
// The definitions this World declared from content, held so they can be redefined
// when their assets reload; one list per kind in kDefinitionKinds.
struct ContentDefinitionLeases
{
    struct Entry
    {
        std::string Path;
        AssetLease Lease;
        std::uint64_t Version = 0;
    };
    std::vector<std::vector<Entry>> Held;
};

std::vector<std::string> ConcretePathsOfSubtype(RuntimeAssets& assets, std::string_view subtype)
{
    std::vector<std::string> paths;
    for (const auto& [path, record] : assets.Registry.Records())
    {
        if (record.Type != AssetType::Data)
            continue;
        const DataAssetEnvelopePeek peek = PeekDataAssetEnvelope(assets.Assets.DefaultSource(), record);
        if (peek.Subtype == subtype && !peek.Abstract)
            paths.push_back(path);
    }
    std::ranges::sort(paths);
    return paths;
}

DataAssetHandle HandleOf(const AssetLease& lease)
{
    return DataAssetHandle::FromToken(lease.OpaqueToken());
}

template <typename T>
const T* Compiled(RuntimeAssets& assets, const AssetLease& lease, std::string_view subtype)
{
    return lease ? assets.DataAssets.TryGet<T>(HandleOf(lease), subtype) : nullptr;
}

// "<path> did not load as <subtype>", with why when the asset system recorded it:
// a definition naming an asset that is missing, or of the wrong kind, says which.
std::string NotLoaded(const RuntimeAssets& assets, std::string_view path, std::string_view subtype)
{
    for (const AssetSystem::LoadFailure& failure : assets.Assets.LoadFailures())
        if (failure.Path == path)
            return std::format("{} did not load as {}: {}", path, subtype, failure.Reason);
    return std::format("{} did not load as {}", path, subtype);
}

// Content's verb bindings compile against the mounted assets, as they run: a
// reference names an asset that is there, of the kind its verb takes.
VerbBindingEnvironment BindingEnvironment(const RuntimeAssets& assets, const World& world)
{
    VerbBindingEnvironment environment = MakeVerbBindingEnvironment(world);
    environment.Assets = &assets.Registry;
    environment.DataAssets = &assets.DataAssets;
    return environment;
}

void DeclareEffect(RuntimeAssets& assets, World& world, ContentDefinitionLeases::Entry& entry,
                   std::vector<std::string>& errors)
{
    const EffectDeclaration* declaration = Compiled<EffectDeclaration>(assets, entry.Lease, kEffectDefinitionType);
    EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
    const AttributeRegistry* attributes = world.TryGetResource<AttributeRegistry>();
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    if (declaration == nullptr || effects == nullptr || attributes == nullptr || tags == nullptr)
    {
        errors.push_back(NotLoaded(assets, entry.Path, kEffectDefinitionType));
        return;
    }
    entry.Version = assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease));

    std::vector<std::string> unresolved;
    EffectDefinition definition;
    if (!ResolveEffectDeclaration(*declaration, *attributes, *tags, definition, unresolved))
    {
        for (const std::string& message : unresolved)
            errors.push_back(std::format("{}: {}", entry.Path, message));
        return;
    }
    const EffectId effect = effects->Define(entry.Path, std::move(definition));
    if (!effect.IsValid())
        errors.push_back(std::format("{}: the effect registry refused it", entry.Path));
    else
        for (const std::string& key : DeclareEffectGrants(world, effect, declaration->GrantedAbilities))
            errors.push_back(std::format("{}: grant '{}' {}", entry.Path, key, kAbilityGrantEntryRefusal));
}

template <typename Registry>
std::string WireKeyCollision(const Registry& registry, std::string_view path)
{
    return std::format("{} travels under the same 32-bit wire key as {}, so a peer could not tell them apart; "
                       "rename one",
                       path, registry.WireKeyHolder(path));
}

// A reload that no longer resolves keeps the definition that last did, and says why.
void DeclareAbility(RuntimeAssets& assets, World& world, ContentDefinitionLeases::Entry& entry,
                    std::vector<std::string>& errors)
{
    const AbilityDeclaration* declaration = Compiled<AbilityDeclaration>(assets, entry.Lease, kAbilityDefinitionType);
    AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
    if (declaration == nullptr || abilities == nullptr)
    {
        errors.push_back(NotLoaded(assets, entry.Path, kAbilityDefinitionType));
        return;
    }
    entry.Version = assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease));

    std::vector<AbilityDiagnostic> diagnostics;
    AbilityDefinition definition;
    AbilityResolveEnvironment environment = MakeAbilityResolveEnvironment(world);
    environment.Verbs = BindingEnvironment(assets, world);
    const bool resolved = ResolveAbilityDeclaration(*declaration, entry.Path, environment, definition, diagnostics);
    for (const AbilityDiagnostic& diagnostic : diagnostics)
        if (diagnostic.Severity == AbilityDiagnosticSeverity::Error)
            errors.push_back(std::format("{}: {} {} {}: {}", entry.Path, diagnostic.Code, diagnostic.ElementKey,
                                         diagnostic.Path, diagnostic.Message));
    const AbilityId ability = resolved ? abilities->Define(entry.Path, std::move(definition))
                                       : abilities->Reserve(entry.Path);
    if (!ability.IsValid())
        errors.push_back(WireKeyCollision(*abilities, entry.Path));
}

void DeclareCuePresentation(RuntimeAssets& assets, World& world, ContentDefinitionLeases::Entry& entry,
                            std::vector<std::string>& errors)
{
    const CuePresentationDeclaration* declaration =
        Compiled<CuePresentationDeclaration>(assets, entry.Lease, kCuePresentationType);
    CuePresentationLibrary* library = world.TryGetResource<CuePresentationLibrary>();
    if (declaration == nullptr || library == nullptr)
    {
        errors.push_back(NotLoaded(assets, entry.Path, kCuePresentationType));
        return;
    }
    entry.Version = assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease));
    library->Declare(entry.Path, *declaration, BindingEnvironment(assets, world), errors);
}

// A reload that no longer resolves keeps the definition that last did, and says why.
void DeclareItem(RuntimeAssets& assets, World& world, ContentDefinitionLeases::Entry& entry,
                 std::vector<std::string>& errors)
{
    const ItemDeclaration* declaration = Compiled<ItemDeclaration>(assets, entry.Lease, kItemDefinitionType);
    ItemRegistry* items = world.TryGetResource<ItemRegistry>();
    if (declaration == nullptr || items == nullptr)
    {
        errors.push_back(NotLoaded(assets, entry.Path, kItemDefinitionType));
        return;
    }
    entry.Version = assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease));
    std::vector<std::string> unresolved;
    ItemDefinition definition;
    const bool resolved = ResolveItemDeclaration(*declaration, world, definition, unresolved);
    for (const std::string& message : unresolved)
        errors.push_back(std::format("{}: {}", entry.Path, message));
    const ItemId item = resolved ? items->Define(entry.Path, std::move(definition)) : items->Reserve(entry.Path);
    if (!item.IsValid())
        errors.push_back(WireKeyCollision(*items, entry.Path));
}

void DeclareEquipmentLayout(RuntimeAssets& assets, World& world, ContentDefinitionLeases::Entry& entry,
                            std::vector<std::string>& errors)
{
    const EquipmentLayoutDeclaration* declaration =
        Compiled<EquipmentLayoutDeclaration>(assets, entry.Lease, kEquipmentLayoutType);
    EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
    if (declaration == nullptr || layouts == nullptr)
    {
        errors.push_back(NotLoaded(assets, entry.Path, kEquipmentLayoutType));
        return;
    }
    entry.Version = assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease));
    std::vector<std::string> unresolved;
    EquipmentLayout layout;
    const bool resolved = ResolveEquipmentLayoutDeclaration(*declaration, world, layout, unresolved);
    for (const std::string& message : unresolved)
        errors.push_back(std::format("{}: {}", entry.Path, message));
    if (resolved)
        (void)RedefineEquipmentLayout(world, entry.Path, std::move(layout));
    else
        (void)layouts->Reserve(entry.Path);
}

using DeclareFn = void (*)(RuntimeAssets&, World&, ContentDefinitionLeases::Entry&, std::vector<std::string>&);

template <typename Registry>
bool Holds(const World& world)
{
    return world.HasResource<Registry>();
}

struct DefinitionKind
{
    std::string_view Subtype;
    DeclareFn Declare;
    bool (*Wanted)(const World&);
};

// Declared in this order: abilities name effects, and items name abilities.
constexpr DefinitionKind kDefinitionKinds[] = {
    { kEffectDefinitionType, &DeclareEffect, &Holds<EffectRegistry> },
    { kAbilityDefinitionType, &DeclareAbility, &Holds<AbilityRegistry> },
    { kCuePresentationType, &DeclareCuePresentation, &Holds<CuePresentationLibrary> },
    { kItemDefinitionType, &DeclareItem, &Holds<ItemRegistry> },
    { kEquipmentLayoutType, &DeclareEquipmentLayout, &Holds<EquipmentLayoutRegistry> },
};

void DeclareAll(RuntimeAssets& assets, World& world, ContentVocabularyReload reload, std::string_view subtype,
                std::vector<ContentDefinitionLeases::Entry>* held, DeclareFn declare, std::vector<std::string>& errors)
{
    for (const std::string& path : ConcretePathsOfSubtype(assets, subtype))
    {
        if (reload == ContentVocabularyReload::Once)
        {
            ContentDefinitionLeases::Entry entry{ path, assets.Assets.LoadLease(path, AssetType::Data), 0 };
            declare(assets, world, entry, errors);
            continue;
        }
        if (std::ranges::any_of(*held, [&](const auto& entry) { return entry.Path == path; }))
            continue;
        held->push_back({ path, assets.Assets.LoadLease(path, AssetType::Data), 0 });
        declare(assets, world, held->back(), errors);
    }
}

// Every content tag declaration, merged.
GameplayTagDeclarations ContentTagDeclarations(RuntimeAssets& assets, std::vector<std::string>& errors)
{
    GameplayTagDeclarations all;
    for (const std::string& path : ConcretePathsOfSubtype(assets, kGameplayTagDeclarationsType))
    {
        // Held only while its names are read: a registered name outlives it.
        const AssetLease lease = assets.Assets.LoadLease(path, AssetType::Data);
        const GameplayTagDeclarations* declarations =
            Compiled<GameplayTagDeclarations>(assets, lease, kGameplayTagDeclarationsType);
        if (declarations == nullptr)
        {
            errors.push_back(std::format("{} did not load as {}", path, kGameplayTagDeclarationsType));
            continue;
        }
        all.Tags.insert(all.Tags.end(), declarations->Tags.begin(), declarations->Tags.end());
        all.ReplicatedToAll.insert(all.ReplicatedToAll.end(), declarations->ReplicatedToAll.begin(),
                                   declarations->ReplicatedToAll.end());
    }
    return all;
}
} // namespace

void CollectContentTags(RuntimeAssets& assets, std::vector<std::string>& names, std::vector<std::string>& errors)
{
    const GameplayTagDeclarations all = ContentTagDeclarations(assets, errors);
    names.insert(names.end(), all.Tags.begin(), all.Tags.end());
    names.insert(names.end(), all.ReplicatedToAll.begin(), all.ReplicatedToAll.end());
}

void DeclareContentTags(RuntimeAssets& assets, GameplayTagRegistry& tags, std::vector<std::string>& errors)
{
    DeclareGameplayTags(ContentTagDeclarations(assets, errors), tags, errors);
}

void DeclareContentVocabulary(RuntimeAssets& assets, World& world, ContentVocabularyReload reload,
                              std::vector<std::string>& errors)
{
    if (GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
        DeclareContentTags(assets, *tags, errors);

    if (AttributeRegistry* attributes = world.TryGetResource<AttributeRegistry>())
    {
        for (const std::string& path : ConcretePathsOfSubtype(assets, kAttributeDeclarationsType))
        {
            const AssetLease lease = assets.Assets.LoadLease(path, AssetType::Data);
            const AttributeDeclarations* declarations =
                Compiled<AttributeDeclarations>(assets, lease, kAttributeDeclarationsType);
            if (declarations == nullptr)
            {
                errors.push_back(std::format("{} did not load as {}", path, kAttributeDeclarationsType));
                continue;
            }
            std::vector<std::string> refused;
            DeclareAttributes(*declarations, *attributes, refused);
            for (const std::string& message : refused)
                errors.push_back(std::format("{}: {}", path, message));
        }
    }

    ContentDefinitionLeases* leases = nullptr;
    if (reload == ContentVocabularyReload::Watch)
    {
        leases = world.HasResource<ContentDefinitionLeases>() ? &world.GetResource<ContentDefinitionLeases>()
                                                              : &world.AddResource<ContentDefinitionLeases>();
        leases->Held.resize(std::size(kDefinitionKinds));
    }
    for (std::size_t kind = 0; kind < std::size(kDefinitionKinds); ++kind)
        if (kDefinitionKinds[kind].Wanted(world))
            DeclareAll(assets, world, reload, kDefinitionKinds[kind].Subtype,
                       leases != nullptr ? &leases->Held[kind] : nullptr, kDefinitionKinds[kind].Declare, errors);
    DeclareWornGrantEntries(world, errors);
}

void RefreshContentVocabulary(RuntimeAssets& assets, World& world, std::vector<std::string>& errors)
{
    ContentDefinitionLeases* leases = world.TryGetResource<ContentDefinitionLeases>();
    if (leases == nullptr)
        return;
    for (std::size_t kind = 0; kind < leases->Held.size(); ++kind)
        for (ContentDefinitionLeases::Entry& entry : leases->Held[kind])
            if (entry.Lease && assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease)) != entry.Version)
                kDefinitionKinds[kind].Declare(assets, world, entry, errors);
    DeclareWornGrantEntries(world, errors);
}

void ReleaseContentVocabulary(World& world)
{
    if (ContentDefinitionLeases* leases = world.TryGetResource<ContentDefinitionLeases>())
        for (std::vector<ContentDefinitionLeases::Entry>& held : leases->Held)
            held.clear();
}
