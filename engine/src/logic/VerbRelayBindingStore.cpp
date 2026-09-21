#include <logic/VerbRelayBindingStore.h>

#include <authored/WorldVocabulary.h>
#include <core/assets/AssetRegistry.h>
#include <ecs/World.h>
#include <logic/VerbRelay.h>

#include <utility>

VerbRelayBindingStore::VerbRelayBindingStore(const AssetRegistry& assets,
                                             DataAssetCache& dataAssets)
    : Assets(assets)
    , DataAssets(dataAssets)
{
}

const CompiledVerbBinding* VerbRelayBindingStore::Resolve(const VerbRelay& relay,
                                                          const World& world,
                                                          std::vector<std::string>* newErrors)
{
    if (!relay.Bindings.IsValid() || !relay.Binding.IsValid())
        return nullptr;

    VerbBindingEnvironment environment = MakeVerbBindingEnvironment(world);
    if (environment.Verbs == nullptr)
        return nullptr;
    environment.Assets = &Assets;
    environment.DataAssets = &DataAssets;

    const DataAssetHandle handle = relay.Bindings;
    Entry& entry = Entries[handle.ToToken()];
    if (!entry.Lease)
    {
        // Held for as long as the entry is, independently of the relays that
        // named it: a relay streamed out and back in must not find its set
        // freed underneath a resolved binding.
        const std::string_view path = DataAssets.GetName(handle);
        if (!path.empty())
            entry.Lease = DataAssets.AcquireOwned(path);
    }

    // The set knows what it was built against and rebuilds itself when any of
    // it moved: the asset, the catalog, the tag vocabulary. The first
    // resolution builds it; every later one asks.
    std::vector<std::string> errors;
    bool rebuilt = false;
    if (!entry.Built)
    {
        entry.Bindings.InstantiateFrom(DataAssets, handle, environment, errors);
        entry.Built = true;
        rebuilt = true;
    }
    else if (entry.Bindings.Refresh(&DataAssets, environment, errors))
    {
        rebuilt = true;
    }

    if (rebuilt)
    {
        ++Rebuilds;
        entry.Errors = std::move(errors);
        entry.ErrorsDelivered = false;
    }
    const CompiledVerbBinding* found = entry.Bindings.Find(relay.Binding);

    if (newErrors != nullptr && !entry.ErrorsDelivered)
    {
        newErrors->insert(newErrors->end(), entry.Errors.begin(), entry.Errors.end());
        entry.ErrorsDelivered = true;
    }
    return found;
}

void VerbRelayBindingStore::Clear()
{
    Entries.clear();
}
