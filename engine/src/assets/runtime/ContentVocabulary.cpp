#include <assets/runtime/ContentVocabulary.h>

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
// The effects this World declared from content, held so they can be redefined
// when their assets reload.
struct ContentEffectLeases
{
    struct Entry
    {
        std::string Path;
        AssetLease Lease;
        std::uint64_t Version = 0;
    };
    std::vector<Entry> Effects;
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

void DeclareEffect(RuntimeAssets& assets, World& world, ContentEffectLeases::Entry& entry,
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
    if (!effects->Define(entry.Path, std::move(definition)).IsValid())
        errors.push_back(std::format("{}: the effect registry refused it", entry.Path));
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

    if (!world.HasResource<EffectRegistry>())
        return;
    if (reload == ContentVocabularyReload::Once)
    {
        for (const std::string& path : PathsOfSubtype(assets, kEffectDefinitionType))
        {
            ContentEffectLeases::Entry entry{ path, assets.Assets.LoadLease(path, AssetType::Data), 0 };
            DeclareEffect(assets, world, entry, errors);
        }
        return;
    }
    ContentEffectLeases& leases = world.HasResource<ContentEffectLeases>()
                                      ? world.GetResource<ContentEffectLeases>()
                                      : world.AddResource<ContentEffectLeases>();
    for (const std::string& path : PathsOfSubtype(assets, kEffectDefinitionType))
    {
        if (std::ranges::any_of(leases.Effects, [&](const auto& entry) { return entry.Path == path; }))
            continue;
        leases.Effects.push_back({ path, assets.Assets.LoadLease(path, AssetType::Data), 0 });
        DeclareEffect(assets, world, leases.Effects.back(), errors);
    }
}

void RefreshContentVocabulary(RuntimeAssets& assets, World& world, std::vector<std::string>& errors)
{
    ContentEffectLeases* leases = world.TryGetResource<ContentEffectLeases>();
    if (leases == nullptr)
        return;
    for (ContentEffectLeases::Entry& entry : leases->Effects)
        if (entry.Lease && assets.DataAssets.GetReloadVersion(HandleOf(entry.Lease)) != entry.Version)
            DeclareEffect(assets, world, entry, errors);
}

void ReleaseContentVocabulary(World& world)
{
    if (ContentEffectLeases* leases = world.TryGetResource<ContentEffectLeases>())
        leases->Effects.clear();
}
