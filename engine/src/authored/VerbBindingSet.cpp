#include <authored/VerbBindingSet.h>

#include <assets/data/DataAssetCache.h>
#include <authored/VerbBindingData.h>

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
            continue;
        }
        CompiledVerbBinding compiled;
        if (CompileVerbBinding(desc, environment, compiled, errors))
            Bindings.push_back(std::move(compiled));
    }
}

void VerbBindingSet::Instantiate(const VerbBindingLibrary& library,
                                 const VerbBindingEnvironment& environment,
                                 std::vector<std::string>& errors)
{
    Bindings.clear();
    Sources.clear();
    Bindings.reserve(library.Bindings.size());
    Compile(library, environment, errors);
    ++Revision_;
}

void VerbBindingSet::Append(const VerbBindingLibrary& library,
                            const VerbBindingEnvironment& environment,
                            std::vector<std::string>& errors)
{
    Compile(library, environment, errors);
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
    if (const VerbBindingLibrary* library = LibraryOf(cache, asset, errors))
        Compile(*library, environment, errors);
    Sources.push_back(Source{ asset, cache.GetReloadVersion(asset) });
    ++Revision_;
}

void VerbBindingSet::AppendFrom(const DataAssetCache& cache,
                                DataAssetHandle asset,
                                const VerbBindingEnvironment& environment,
                                std::vector<std::string>& errors)
{
    if (const VerbBindingLibrary* library = LibraryOf(cache, asset, errors))
        Compile(*library, environment, errors);
    Sources.push_back(Source{ asset, cache.GetReloadVersion(asset) });
    ++Revision_;
}

bool VerbBindingSet::Refresh(const DataAssetCache& cache,
                             const VerbBindingEnvironment& environment,
                             std::vector<std::string>& errors)
{
    bool changed = false;
    for (const Source& source : Sources)
        changed = changed || cache.GetReloadVersion(source.Asset) != source.ReloadVersion;
    if (!changed)
        return false;

    // Rebuilt whole, in source order, so a record that moved between files
    // or vanished from one lands exactly as a fresh instantiation would.
    std::vector<Source> sources = std::move(Sources);
    Bindings.clear();
    Sources.clear();
    for (Source& source : sources)
    {
        if (const VerbBindingLibrary* library = LibraryOf(cache, source.Asset, errors))
            Compile(*library, environment, errors);
        Sources.push_back(Source{ source.Asset, cache.GetReloadVersion(source.Asset) });
    }
    ++Revision_;
    return true;
}

void VerbBindingSet::Clear()
{
    Bindings.clear();
    Sources.clear();
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
