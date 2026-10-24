#include <logic/VerbSequenceStore.h>

#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/VerbSequenceCompiler.h>

#include <memory>

VerbSequenceStore::VerbSequenceStore(std::span<const SequencePort> ports)
    : PortTable(ports.begin(), ports.end())
{
}

VerbSequenceHandle VerbSequenceStore::Intern(const JsonValue& authored)
{
    std::string text = JsonStringify(authored);
    if (const auto found = ByText.find(text); found != ByText.end())
    {
        Retain(found->second);
        return found->second;
    }

    auto entry = std::make_unique<Entry>();
    entry->Text = text;
    entry->Source = authored;
    entry->References = 1;
    std::vector<AssetRef> dependencies;
    (void)ReadVerbSequence(authored, entry->Desc, dependencies, entry->ReadErrors);
    const VerbSequenceHandle handle = Entries.Adopt(std::move(entry));
    ByText.emplace(std::move(text), handle);
    return handle;
}

void VerbSequenceStore::Retain(VerbSequenceHandle sequence)
{
    if (Entry* entry = Entries.Find(sequence))
        ++entry->References;
}

void VerbSequenceStore::Release(VerbSequenceHandle sequence)
{
    Entry* entry = Entries.Find(sequence);
    if (entry == nullptr || --entry->References > 0)
        return;
    ByText.erase(entry->Text);
    (void)Entries.Erase(sequence);
}

const JsonValue* VerbSequenceStore::Source(VerbSequenceHandle sequence) const
{
    const Entry* entry = Entries.Find(sequence);
    return entry != nullptr ? &entry->Source : nullptr;
}

VerbSequenceStore::Stamp VerbSequenceStore::StampOf(const World& world)
{
    Stamp stamp;
    if (const VerbRegistry* verbs = FindVerbRegistry(world))
        stamp.Verbs = CatalogStamp::Of(*verbs);
    if (const AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world))
        stamp.Queries = CatalogStamp::Of(*queries);
    if (const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
        stamp.Tags = TagVocabularyStamp::Of(*tags).Count;
    return stamp;
}

const CompiledVerbSequence* VerbSequenceStore::Resolve(VerbSequenceHandle sequence,
                                                       const World& world,
                                                       const AssetRegistry* assets,
                                                       const DataAssetCache* dataAssets)
{
    Entry* entry = Entries.Find(sequence);
    if (entry == nullptr)
        return nullptr;
    const Stamp now = StampOf(world);
    if (entry->Built && entry->CompiledAgainst == now)
        return &entry->Compiled;

    VerbSequenceEnvironment environment;
    environment.Bindings.Verbs = FindVerbRegistry(world);
    environment.Bindings.Tags = world.TryGetResource<GameplayTagRegistry>();
    environment.Bindings.Assets = assets;
    environment.Bindings.DataAssets = dataAssets;
    environment.Queries = FindAuthoredQueryRegistry(world);
    environment.Components = &world;
    environment.Ports = PortTable;

    entry->Diagnostics = entry->ReadErrors;
    CompileVerbSequence(entry->Desc, environment, entry->Compiled, entry->Diagnostics);
    entry->CompiledAgainst = now;
    entry->Built = true;
    ++Compiles;
    return &entry->Compiled;
}

std::span<const std::string> VerbSequenceStore::Diagnostics(VerbSequenceHandle sequence) const
{
    const Entry* entry = Entries.Find(sequence);
    if (entry == nullptr)
        return {};
    return entry->Built ? std::span<const std::string>(entry->Diagnostics)
                        : std::span<const std::string>(entry->ReadErrors);
}
