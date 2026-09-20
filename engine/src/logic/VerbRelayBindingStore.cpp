#include <logic/VerbRelayBindingStore.h>

#include <authored/VerbBindingCompiler.h>
#include <authored/VerbBindingData.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <logic/VerbRelay.h>

#include <format>
#include <utility>

VerbRelayBindingStore::VerbRelayBindingStore(DataAssetCache& dataAssets)
    : DataAssets(dataAssets)
{
}

const CompiledVerbBinding* VerbRelayBindingStore::Resolve(const VerbRelay& relay,
                                                          const World& world,
                                                          std::vector<std::string>* newErrors)
{
    if (!relay.Bindings.IsValid() || !relay.Binding.IsValid())
        return nullptr;

    const VerbBindingEnvironment environment = MakeVerbBindingEnvironment(world);
    if (environment.Verbs == nullptr)
        return nullptr;

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

    const std::uint64_t reloadVersion = DataAssets.GetReloadVersion(handle);
    const CompiledVerbBinding* found = entry.Bindings.Find(relay.Binding);
    const bool stale = found != nullptr && !IsVerbBindingCurrent(*found, *environment.Verbs);
    const bool rebuild = entry.ReloadVersion != reloadVersion
        || entry.Catalog != environment.Verbs->Catalog() || stale;

    if (rebuild)
    {
        ++Rebuilds;
        entry.ReloadVersion = reloadVersion;
        entry.Catalog = environment.Verbs->Catalog();
        entry.Errors.clear();
        entry.ErrorsDelivered = false;
        entry.Bindings.Clear();

        const auto* library =
            DataAssets.TryGet<VerbBindingLibrary>(handle, kVerbBindingsTypeName);
        if (library == nullptr)
        {
            entry.Errors.push_back(std::format("'{}' is stale or is not an authored binding set",
                                               DataAssets.GetName(handle)));
        }
        else
        {
            entry.Bindings.Instantiate(*library, environment, entry.Errors);
        }
        found = entry.Bindings.Find(relay.Binding);
    }

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
