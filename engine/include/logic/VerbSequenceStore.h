#pragma once

#include <authored/BindingDependencyStamp.h>
#include <core/json/JsonValue.h>
#include <logic/VerbSequence.h>
#include <logic/VerbSequenceKey.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class AssetRegistry;
class DataAssetCache;
class World;

// One producer's sequences by content key. A sequence reads the producer's own
// ports first, then -- when it was interned under a declared event -- one port
// per field of that event's payload, so the same text under two events is two
// entries. An entry outlives every component that names it -- it is kept for
// the World's lifetime, so a copied or restored key always resolves -- and the
// store grows with the distinct texts interned.
class VerbSequenceStore
{
public:
    explicit VerbSequenceStore(std::span<const SequencePort> ports);

    [[nodiscard]] VerbSequenceKey Intern(const JsonValue& authored, std::string_view event = {});

    [[nodiscard]] const JsonValue* Source(VerbSequenceKey sequence) const;

    // Reads what every sequence compiles against -- the verb, query and event
    // catalogs and the tag vocabulary -- once. Resolve compares against it, so
    // call at a pass boundary, before resolving.
    void Revalidate(const World& world);

    // Null for a key this store never interned. Compiles first when nothing has,
    // or when what Revalidate read, or a data asset the sequence names, has
    // moved since. A warm resolve allocates nothing.
    [[nodiscard]] const CompiledVerbSequence* Resolve(VerbSequenceKey sequence,
                                                      const World& world,
                                                      const AssetRegistry* assets,
                                                      const DataAssetCache* dataAssets);

    // What reading and the last compile reported, for an inspector or a log.
    [[nodiscard]] std::span<const std::string> Diagnostics(VerbSequenceKey sequence) const;

    // The ports a sequence interned under `event` reads, as `world` declares
    // that event now: the store's own, then the payload's. `names` backs the
    // ports' views.
    [[nodiscard]] std::span<const SequencePort> FixedPorts() const { return PortTable; }
    static void PortsFor(std::span<const SequencePort> fixed, const World& world, std::string_view event,
                         std::vector<std::string>& names, std::vector<SequencePort>& out);

    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] std::uint64_t CompileCount() const { return Compiles; }

private:
    struct Stamp
    {
        CatalogStamp Verbs;
        CatalogStamp Queries;
        CatalogStamp Events;
        std::size_t Tags = 0;
        bool operator==(const Stamp&) const = default;
    };

    struct Entry
    {
        std::string Text;
        std::string Event;
        JsonValue Source;
        VerbSequenceDesc Desc;
        std::vector<AssetRef> DataDependencies;
        std::vector<std::string> ReadErrors;
        CompiledVerbSequence Compiled;
        std::vector<std::string> Diagnostics;
        std::vector<std::string> PortNames;
        std::vector<SequencePort> Ports;
        Stamp CompiledAgainst;
        // One per data dependency, as the cache held it at the compile.
        std::vector<DataAssetStamp> DataStamps;
        bool Built = false;
    };

    [[nodiscard]] static Stamp StampOf(const World& world);
    [[nodiscard]] static bool DataCurrent(const Entry& entry, const DataAssetCache* dataAssets);

    std::vector<SequencePort> PortTable;
    std::unordered_map<VerbSequenceKey, Entry> Entries;
    Stamp Current;
    const World* RevalidatedFor = nullptr;
    std::uint64_t Compiles = 0;
};
