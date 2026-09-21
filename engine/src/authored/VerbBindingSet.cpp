#include <authored/VerbBindingSet.h>

#include <assets/data/DataAssetCache.h>
#include <authored/VerbBindingData.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>
#include <utility>

void VerbBindingSet::Compile(const VerbBindingLibrary& library,
                             const VerbBindingEnvironment& environment,
                             std::vector<std::string>& errors)
{
    for (const VerbBindingDesc& desc : library.Bindings)
    {
        if (Find(desc.KeyId) != nullptr)
        {
            errors.push_back("binding '" + desc.Key + "' is already in this set");
            Unresolved = true;
            continue;
        }
        CompiledVerbBinding compiled;
        if (CompileVerbBinding(desc, environment, compiled, errors))
            Bindings.push_back(std::move(compiled));
        else
            Unresolved = true;
    }
}

void VerbBindingSet::Snapshot(const VerbBindingEnvironment& environment)
{
    Catalog = environment.Verbs != nullptr ? environment.Verbs->Catalog() : VerbCatalogId{};
    CatalogGeneration = environment.Verbs != nullptr ? environment.Verbs->Generation() : 0;
    TagCount = environment.Tags != nullptr ? environment.Tags->Size() : 0;
}

void VerbBindingSet::Instantiate(const VerbBindingLibrary& library,
                                 const VerbBindingEnvironment& environment,
                                 std::vector<std::string>& errors)
{
    Bindings.clear();
    Sources.clear();
    Unresolved = false;
    Bindings.reserve(library.Bindings.size());
    Compile(library, environment, errors);
    Sources.push_back(Source{ .Asset = {}, .ReloadVersion = 0, .Library = library });
    Snapshot(environment);
    ++Revision_;
}

void VerbBindingSet::Append(const VerbBindingLibrary& library,
                            const VerbBindingEnvironment& environment,
                            std::vector<std::string>& errors)
{
    Compile(library, environment, errors);
    Sources.push_back(Source{ .Asset = {}, .ReloadVersion = 0, .Library = library });
    Snapshot(environment);
    ++Revision_;
}

namespace
{
    const VerbBindingLibrary* LibraryOf(const DataAssetCache& cache,
                                        DataAssetHandle asset,
                                        std::vector<std::string>& errors)
    {
        const auto* library = cache.TryGet<VerbBindingLibrary>(asset, kVerbBindingsTypeName);
        if (library == nullptr)
        {
            errors.push_back(std::format("'{}' is not a resident authored binding set",
                                         cache.GetName(asset)));
        }
        return library;
    }
}

void VerbBindingSet::InstantiateFrom(const DataAssetCache& cache,
                                     DataAssetHandle asset,
                                     const VerbBindingEnvironment& environment,
                                     std::vector<std::string>& errors)
{
    Bindings.clear();
    Sources.clear();
    Unresolved = false;
    if (const VerbBindingLibrary* library = LibraryOf(cache, asset, errors))
        Compile(*library, environment, errors);
    else
        Unresolved = true;
    Sources.push_back(Source{ .Asset = asset, .ReloadVersion = cache.GetReloadVersion(asset), .Library = {} });
    Snapshot(environment);
    ++Revision_;
}

void VerbBindingSet::AppendFrom(const DataAssetCache& cache,
                                DataAssetHandle asset,
                                const VerbBindingEnvironment& environment,
                                std::vector<std::string>& errors)
{
    if (const VerbBindingLibrary* library = LibraryOf(cache, asset, errors))
        Compile(*library, environment, errors);
    else
        Unresolved = true;
    Sources.push_back(Source{ .Asset = asset, .ReloadVersion = cache.GetReloadVersion(asset), .Library = {} });
    Snapshot(environment);
    ++Revision_;
}

void VerbBindingSet::Rebuild(const DataAssetCache* cache,
                             const VerbBindingEnvironment& environment,
                             std::vector<std::string>& errors)
{
    // Whole, in contribution order, so a record that moved between files or
    // vanished from one lands exactly as a fresh instantiation would.
    std::vector<Source> sources = std::move(Sources);
    Bindings.clear();
    Sources.clear();
    Unresolved = false;
    for (Source& source : sources)
    {
        if (source.Asset.IsValid())
        {
            const VerbBindingLibrary* library =
                cache != nullptr ? LibraryOf(*cache, source.Asset, errors) : nullptr;
            if (library != nullptr)
                Compile(*library, environment, errors);
            else
                Unresolved = true;
            source.ReloadVersion = cache != nullptr ? cache->GetReloadVersion(source.Asset) : 0;
        }
        else
        {
            Compile(source.Library, environment, errors);
        }
        Sources.push_back(std::move(source));
    }
    Snapshot(environment);
    ++Revision_;
}

bool VerbBindingSet::Refresh(const DataAssetCache* cache,
                             const VerbBindingEnvironment& environment,
                             std::vector<std::string>& errors)
{
    bool changed = false;
    for (const Source& source : Sources)
    {
        if (source.Asset.IsValid() && cache != nullptr)
            changed = changed || cache->GetReloadVersion(source.Asset) != source.ReloadVersion;
    }
    if (environment.Verbs != nullptr)
    {
        changed = changed || environment.Verbs->Catalog() != Catalog
            || environment.Verbs->Generation() != CatalogGeneration;
    }
    if (environment.Tags != nullptr)
        changed = changed || environment.Tags->Size() != TagCount;
    if (!changed)
        return false;

    Rebuild(cache, environment, errors);
    return true;
}

void VerbBindingSet::Clear()
{
    Bindings.clear();
    Sources.clear();
    Unresolved = false;
    ++Revision_;
}

const CompiledVerbBinding* VerbBindingSet::Find(VerbBindingKey key) const
{
    if (!key.IsValid())
        return nullptr;
    for (const CompiledVerbBinding& binding : Bindings)
    {
        if (binding.Key == key)
            return &binding;
    }
    return nullptr;
}

const CompiledVerbBinding* VerbBindingSet::Find(std::string_view key) const
{
    return Find(MakeVerbBindingKey(key));
}
