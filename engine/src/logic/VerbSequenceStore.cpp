#include <logic/VerbSequenceStore.h>

#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/hash/Fnv1a.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/VerbSequenceCompiler.h>

#include <algorithm>
#include <format>

namespace
{
VerbSequenceKey KeyOf(std::string_view event, const std::string& text)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aBytes(hash, event.data(), event.size());
    HashFnv1aByte(hash, 0);
    HashFnv1aBytes(hash, text.data(), text.size());
    return VerbSequenceKey{ hash != 0 ? hash : 1 };
}
} // namespace

VerbSequenceStore::VerbSequenceStore(std::span<const SequencePort> ports)
    : PortTable(ports.begin(), ports.end())
{
}

VerbSequenceKey VerbSequenceStore::Intern(const JsonValue& authored, std::string_view event)
{
    std::string text = JsonStringify(authored);
    VerbSequenceKey key = KeyOf(event, text);
    // Two texts hashing alike is refused rather than answered with the wrong one.
    while (true)
    {
        const auto found = Entries.find(key);
        if (found == Entries.end())
            break;
        if (found->second.Text == text && found->second.Event == event)
            return key;
        key = VerbSequenceKey{ key.Value + 1 != 0 ? key.Value + 1 : 1 };
    }

    Entry& entry = Entries[key];
    entry.Text = std::move(text);
    entry.Event = std::string(event);
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

VerbSequenceStore::Stamp VerbSequenceStore::StampOf(const World& world)
{
    Stamp stamp;
    if (const VerbRegistry* verbs = FindVerbRegistry(world))
        stamp.Verbs = CatalogStamp::Of(*verbs);
    if (const AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world))
        stamp.Queries = CatalogStamp::Of(*queries);
    if (const AuthoredEventRegistry* events = FindAuthoredEventRegistry(world))
        stamp.Events = CatalogStamp::Of(*events);
    if (const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
        stamp.Tags = TagVocabularyStamp::Of(*tags).Count;
    return stamp;
}

void VerbSequenceStore::Revalidate(const World& world)
{
    Current = StampOf(world);
    RevalidatedFor = &world;
}

// A data reference compiles only against a resident asset, so one that arrives
// or reloads later is a reason to compile again.
bool VerbSequenceStore::DataCurrent(const Entry& entry, const DataAssetCache* dataAssets)
{
    if (dataAssets == nullptr)
        return entry.DataStamps.empty() || std::ranges::all_of(entry.DataStamps, [](const DataAssetStamp& stamp) {
                   return !stamp.Asset.IsValid();
               });
    for (std::size_t i = 0; i < entry.DataStamps.size(); ++i)
    {
        const DataAssetStamp& stamp = entry.DataStamps[i];
        const bool current = stamp.Asset.IsValid() ? stamp.Matches(*dataAssets)
                                                   : !dataAssets->Find(entry.DataDependencies[i].Path).IsValid();
        if (!current)
            return false;
    }
    return true;
}

void VerbSequenceStore::PortsFor(std::span<const SequencePort> fixed, const World& world, std::string_view event,
                                 std::vector<std::string>& names, std::vector<SequencePort>& out)
{
    out.assign(fixed.begin(), fixed.end());
    names.clear();
    const AuthoredEventRegistry* events = event.empty() ? nullptr : FindAuthoredEventRegistry(world);
    const AuthoredEventDefinition* definition = events != nullptr ? events->Get(events->Find(event)) : nullptr;
    if (definition == nullptr)
        return;
    // Every name first, so the views into them are taken once nothing moves.
    names.reserve(definition->Payload.Children.size());
    for (const DataFieldSchema& field : definition->Payload.Children)
        names.push_back(field.Key);
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        const DataFieldSchema& field = definition->Payload.Children[i];
        out.push_back(SequencePort{ .Name = names[i], .Kind = field.Kind, .VectorLength = field.VectorLength });
    }
}

const CompiledVerbSequence* VerbSequenceStore::Resolve(VerbSequenceKey sequence,
                                                       const World& world,
                                                       const AssetRegistry* assets,
                                                       const DataAssetCache* dataAssets)
{
    const auto found = Entries.find(sequence);
    if (found == Entries.end())
        return nullptr;
    if (RevalidatedFor != &world)
        Revalidate(world);
    Entry& entry = found->second;
    if (entry.Built && entry.CompiledAgainst == Current && DataCurrent(entry, dataAssets))
        return &entry.Compiled;

    PortsFor(PortTable, world, entry.Event, entry.PortNames, entry.Ports);
    VerbSequenceEnvironment environment;
    environment.Bindings.Verbs = FindVerbRegistry(world);
    environment.Bindings.Tags = world.TryGetResource<GameplayTagRegistry>();
    environment.Bindings.Assets = assets;
    environment.Bindings.DataAssets = dataAssets;
    environment.Queries = FindAuthoredQueryRegistry(world);
    environment.Components = &world;
    environment.Ports = entry.Ports;

    entry.Diagnostics = entry.ReadErrors;
    if (!entry.Event.empty() && entry.Ports.size() == PortTable.size())
    {
        const AuthoredEventRegistry* events = FindAuthoredEventRegistry(world);
        if (events == nullptr || !events->Find(entry.Event).IsValid())
            entry.Diagnostics.push_back(std::format("no event '{}' to read ports from", entry.Event));
    }
    CompileVerbSequence(entry.Desc, environment, entry.Compiled, entry.Diagnostics);
    entry.DataStamps.clear();
    for (const AssetRef& dependency : entry.DataDependencies)
        entry.DataStamps.push_back(dataAssets != nullptr
                                       ? DataAssetStamp::Of(*dataAssets, dataAssets->Find(dependency.Path))
                                       : DataAssetStamp{});
    entry.CompiledAgainst = Current;
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
