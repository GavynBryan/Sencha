#include <logic/VerbSequenceStore.h>

#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/hash/Fnv1a.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/VerbSequenceCompiler.h>

#include <algorithm>

namespace
{
VerbSequenceKey KeyOf(const std::string& text)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aBytes(hash, text.data(), text.size());
    return VerbSequenceKey{ hash != 0 ? hash : 1 };
}
} // namespace

bool VerbSequenceStore::Stamp::operator==(const Stamp& other) const
{
    return Verbs == other.Verbs && Queries == other.Queries && Tags == other.Tags
        && std::ranges::equal(DataAssets, other.DataAssets, [](const DataAssetStamp& a, const DataAssetStamp& b) {
               return a.Asset == b.Asset && a.ReloadVersion == b.ReloadVersion;
           });
}

VerbSequenceStore::VerbSequenceStore(std::span<const SequencePort> ports)
    : PortTable(ports.begin(), ports.end())
{
}

VerbSequenceKey VerbSequenceStore::Intern(const JsonValue& authored)
{
    std::string text = JsonStringify(authored);
    VerbSequenceKey key = KeyOf(text);
    // Two texts hashing alike is refused rather than answered with the wrong one.
    while (true)
    {
        const auto found = Entries.find(key);
        if (found == Entries.end())
            break;
        if (found->second.Text == text)
            return key;
        key = VerbSequenceKey{ key.Value + 1 != 0 ? key.Value + 1 : 1 };
    }

    Entry& entry = Entries[key];
    entry.Text = std::move(text);
    entry.Source = authored;
    std::vector<AssetRef> dependencies;
    (void)ReadVerbSequence(authored, entry.Desc, dependencies, entry.ReadErrors);
    for (AssetRef& dependency : dependencies)
        if (dependency.Type == AssetType::Data)
            entry.DataDependencies.push_back(std::move(dependency));
    return key;
}

const JsonValue* VerbSequenceStore::Source(VerbSequenceKey sequence) const
{
    const auto found = Entries.find(sequence);
    return found != Entries.end() ? &found->second.Source : nullptr;
}

VerbSequenceStore::Stamp VerbSequenceStore::StampOf(const World& world, const Entry& entry,
                                                    const DataAssetCache* dataAssets)
{
    Stamp stamp;
    if (const VerbRegistry* verbs = FindVerbRegistry(world))
        stamp.Verbs = CatalogStamp::Of(*verbs);
    if (const AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world))
        stamp.Queries = CatalogStamp::Of(*queries);
    if (const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
        stamp.Tags = TagVocabularyStamp::Of(*tags).Count;
    // A data reference compiles only against a resident asset, so one that
    // arrives or reloads later is a reason to compile again.
    if (dataAssets != nullptr)
        for (const AssetRef& dependency : entry.DataDependencies)
            stamp.DataAssets.push_back(DataAssetStamp::Of(*dataAssets, dataAssets->Find(dependency.Path)));
    return stamp;
}

const CompiledVerbSequence* VerbSequenceStore::Resolve(VerbSequenceKey sequence,
                                                       const World& world,
                                                       const AssetRegistry* assets,
                                                       const DataAssetCache* dataAssets)
{
    const auto found = Entries.find(sequence);
    if (found == Entries.end())
        return nullptr;
    Entry& entry = found->second;
    Stamp now = StampOf(world, entry, dataAssets);
    if (entry.Built && entry.CompiledAgainst == now)
        return &entry.Compiled;

    VerbSequenceEnvironment environment;
    environment.Bindings.Verbs = FindVerbRegistry(world);
    environment.Bindings.Tags = world.TryGetResource<GameplayTagRegistry>();
    environment.Bindings.Assets = assets;
    environment.Bindings.DataAssets = dataAssets;
    environment.Queries = FindAuthoredQueryRegistry(world);
    environment.Components = &world;
    environment.Ports = PortTable;

    entry.Diagnostics = entry.ReadErrors;
    CompileVerbSequence(entry.Desc, environment, entry.Compiled, entry.Diagnostics);
    entry.CompiledAgainst = std::move(now);
    entry.Built = true;
    ++Compiles;
    return &entry.Compiled;
}

std::span<const std::string> VerbSequenceStore::Diagnostics(VerbSequenceKey sequence) const
{
    const auto found = Entries.find(sequence);
    if (found == Entries.end())
        return {};
    const Entry& entry = found->second;
    return entry.Built ? std::span<const std::string>(entry.Diagnostics)
                       : std::span<const std::string>(entry.ReadErrors);
}
