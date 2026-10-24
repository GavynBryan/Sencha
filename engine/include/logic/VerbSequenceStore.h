#pragma once

#include <authored/BindingDependencyStamp.h>
#include <core/json/JsonValue.h>
#include <logic/VerbSequence.h>
#include <logic/VerbSequenceKey.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class AssetRegistry;
class DataAssetCache;
class World;

// One producer's sequences by content key. An entry outlives every component
// that names it -- it is kept for the World's lifetime, so a copied or restored
// key always resolves -- and the store grows with the distinct texts interned.
class VerbSequenceStore
{
public:
    explicit VerbSequenceStore(std::span<const SequencePort> ports);

    [[nodiscard]] VerbSequenceKey Intern(const JsonValue& authored);

    [[nodiscard]] const JsonValue* Source(VerbSequenceKey sequence) const;

    // Null for a key this store never interned. Compiles first when nothing has,
    // or when a catalog, the tags or a data asset it names has moved since.
    [[nodiscard]] const CompiledVerbSequence* Resolve(VerbSequenceKey sequence,
                                                      const World& world,
                                                      const AssetRegistry* assets,
                                                      const DataAssetCache* dataAssets);

    // What reading and the last compile reported, for an inspector or a log.
    [[nodiscard]] std::span<const std::string> Diagnostics(VerbSequenceKey sequence) const;

    [[nodiscard]] std::span<const SequencePort> Ports() const { return PortTable; }
    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] std::uint64_t CompileCount() const { return Compiles; }

private:
    struct Stamp
    {
        CatalogStamp Verbs;
        CatalogStamp Queries;
        std::size_t Tags = 0;
        std::vector<DataAssetStamp> DataAssets;
        bool operator==(const Stamp&) const;
    };

    struct Entry
    {
        std::string Text;
        JsonValue Source;
        VerbSequenceDesc Desc;
        std::vector<AssetRef> DataDependencies;
        std::vector<std::string> ReadErrors;
        CompiledVerbSequence Compiled;
        std::vector<std::string> Diagnostics;
        Stamp CompiledAgainst;
        bool Built = false;
    };

    [[nodiscard]] static Stamp StampOf(const World& world, const Entry& entry, const DataAssetCache* dataAssets);

    std::vector<SequencePort> PortTable;
    std::unordered_map<VerbSequenceKey, Entry> Entries;
    std::uint64_t Compiles = 0;
};
