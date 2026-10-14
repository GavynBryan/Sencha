#include <assets/runtime/ContentVocabulary.h>

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityRegistry.h>
#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/RuntimeAssets.h>
#include <attributes/AttributeDeclarations.h>
#include <attributes/AttributeRegistry.h>
#include <core/assets/AssetRegistry.h>
#include <ecs/World.h>
#include <effects/EffectDefinitionData.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagDeclarations.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <string_view>

namespace
{
// The definitions this World declared from content, held so they can be redefined
// when their assets reload. Effects before abilities, since abilities name effects.
struct ContentDefinitionLeases
{
    struct Entry
    {
        std::string Path;
        AssetLease Lease;
        std::uint64_t Version = 0;
    };
    std::vector<Entry> Effects;
    std::vector<Entry> Abilities;
};

std::vector<std::string> PathsOfSubtype(RuntimeAssets& assets, std::string_view subtype)
{
    std::vector<std::string> paths;
    for (const auto& [path, record] : assets.Registry.Records())
        if (record.Type == AssetType::Data && PeekDataAssetSubtype(assets.Assets.DefaultSource(), record) == subtype)
            paths.push_back(path);
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

void DeclareEffect(RuntimeAssets& assets, World& world, ContentDefinitionLeases::Entry& entry,
                   std::vector<std::string>& errors)
{
    const EffectDeclaration* declaration = Compiled<EffectDeclaration>(assets, entry.Lease, kEffectDefinitionType);
    EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
    const AttributeRegistry* attributes = world.TryGetResource<AttributeRegistry>();
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    if (declaration == nullptr || effects == nullptr || attributes == nullptr || tags == nullptr)
    {
        errors.push_back(std::format("{} did not load as {}", entry.Path, kEffectDefinitionType));
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
        DeclareEffectGrants(world, effect, declaration->GrantedAbilities);
}

// A reload that no longer resolves keeps the definition that last did, and says why.
void DeclareAbility(RuntimeAssets& assets, World& world, ContentDefinitionLeases::Entry& entry,
                    std::vector<std::string>& errors)
{
    const AbilityDeclaration* declaration = Compiled<AbilityDeclaration>(assets, entry.Lease, kAbilityDefinitionType);
    AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
    if (declaration == nullptr || abilities == nullptr)
    {
        errors.push_back(std::format("{} did not load as {}", entry.Path, kAbilityDefinitionType));
        return;
    }
    entry.Version = assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease));

    std::vector<AbilityDiagnostic> diagnostics;
    AbilityDefinition definition;
    const bool resolved = ResolveAbilityDeclaration(*declaration, entry.Path, MakeAbilityResolveEnvironment(world),
                                                    definition, diagnostics);
    for (const AbilityDiagnostic& diagnostic : diagnostics)
        if (diagnostic.Severity == AbilityDiagnosticSeverity::Error)
            errors.push_back(std::format("{}: {} {} {}: {}", entry.Path, diagnostic.Code, diagnostic.ElementKey,
                                         diagnostic.Path, diagnostic.Message));
    if (resolved)
        (void)abilities->Define(entry.Path, std::move(definition));
    else
        (void)abilities->Reserve(entry.Path);
}

using DeclareFn = void (*)(RuntimeAssets&, World&, ContentDefinitionLeases::Entry&, std::vector<std::string>&);

void DeclareAll(RuntimeAssets& assets, World& world, ContentVocabularyReload reload, std::string_view subtype,
                std::vector<ContentDefinitionLeases::Entry>* held, DeclareFn declare, std::vector<std::string>& errors)
{
    for (const std::string& path : PathsOfSubtype(assets, subtype))
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
} // namespace

void CollectContentTags(RuntimeAssets& assets, std::vector<std::string>& names, std::vector<std::string>& errors)
{
    for (const std::string& path : PathsOfSubtype(assets, kGameplayTagDeclarationsType))
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
        names.insert(names.end(), declarations->Tags.begin(), declarations->Tags.end());
    }
}

void DeclareContentTags(RuntimeAssets& assets, GameplayTagRegistry& tags, std::vector<std::string>& errors)
{
    GameplayTagDeclarations all;
    CollectContentTags(assets, all.Tags, errors);
    DeclareGameplayTags(all, tags, errors);
}

void DeclareContentVocabulary(RuntimeAssets& assets, World& world, ContentVocabularyReload reload,
                              std::vector<std::string>& errors)
{
    if (GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
        DeclareContentTags(assets, *tags, errors);

    if (AttributeRegistry* attributes = world.TryGetResource<AttributeRegistry>())
    {
        for (const std::string& path : PathsOfSubtype(assets, kAttributeDeclarationsType))
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
        leases = world.HasResource<ContentDefinitionLeases>() ? &world.GetResource<ContentDefinitionLeases>()
                                                              : &world.AddResource<ContentDefinitionLeases>();
    if (world.HasResource<EffectRegistry>())
        DeclareAll(assets, world, reload, kEffectDefinitionType, leases != nullptr ? &leases->Effects : nullptr,
                   &DeclareEffect, errors);
    if (world.HasResource<AbilityRegistry>())
        DeclareAll(assets, world, reload, kAbilityDefinitionType, leases != nullptr ? &leases->Abilities : nullptr,
                   &DeclareAbility, errors);
}

void RefreshContentVocabulary(RuntimeAssets& assets, World& world, std::vector<std::string>& errors)
{
    ContentDefinitionLeases* leases = world.TryGetResource<ContentDefinitionLeases>();
    if (leases == nullptr)
        return;
    const auto refresh = [&](std::vector<ContentDefinitionLeases::Entry>& entries, DeclareFn declare) {
        for (ContentDefinitionLeases::Entry& entry : entries)
            if (entry.Lease && assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease)) != entry.Version)
                declare(assets, world, entry, errors);
    };
    refresh(leases->Effects, &DeclareEffect);
    refresh(leases->Abilities, &DeclareAbility);
}

void ReleaseContentVocabulary(World& world)
{
    if (ContentDefinitionLeases* leases = world.TryGetResource<ContentDefinitionLeases>())
    {
        leases->Effects.clear();
        leases->Abilities.clear();
    }
}
