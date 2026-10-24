#pragma once

#include <authored/BindingDependencyStamp.h>
#include <core/handle/HandlePool.h>
#include <core/json/JsonValue.h>
#include <logic/VerbSequence.h>
#include <logic/VerbSequenceHandle.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class AssetRegistry;
class DataAssetCache;
class World;

// One producer's sequences, interned and reference-counted by the components
// that hold their handles; the authored text is kept verbatim and the compiled
// form rebuilt when a catalog or the tags move. Owner-thread only.
class VerbSequenceStore
{
public:
    explicit VerbSequenceStore(std::span<const SequencePort> ports);

    // One reference to the sequence `authored` spells, owned by the caller.
    [[nodiscard]] VerbSequenceHandle Intern(const JsonValue& authored);
    void Retain(VerbSequenceHandle sequence);
    void Release(VerbSequenceHandle sequence);

    [[nodiscard]] const JsonValue* Source(VerbSequenceHandle sequence) const;

    // Null for a stale handle. Compiles first when nothing has, or when what it
    // compiled against has moved.
    [[nodiscard]] const CompiledVerbSequence* Resolve(VerbSequenceHandle sequence,
                                                      const World& world,
                                                      const AssetRegistry* assets,
                                                      const DataAssetCache* dataAssets);

    // What reading and the last compile reported, for an inspector or a log.
    [[nodiscard]] std::span<const std::string> Diagnostics(VerbSequenceHandle sequence) const;

    [[nodiscard]] std::span<const SequencePort> Ports() const { return PortTable; }
    [[nodiscard]] std::size_t Size() const { return Entries.Size(); }
    [[nodiscard]] std::uint64_t CompileCount() const { return Compiles; }

private:
    struct Stamp
    {
        CatalogStamp Verbs;
        CatalogStamp Queries;
        std::size_t Tags = 0;
        bool operator==(const Stamp&) const = default;
    };

    struct Entry
    {
        std::string Text;
        JsonValue Source;
        std::uint32_t References = 0;
        VerbSequenceDesc Desc;
        std::vector<std::string> ReadErrors;
        CompiledVerbSequence Compiled;
        std::vector<std::string> Diagnostics;
        Stamp CompiledAgainst;
        bool Built = false;
    };

    [[nodiscard]] static Stamp StampOf(const World& world);

    std::vector<SequencePort> PortTable;
    HandlePool<VerbSequenceTag, Entry> Entries;
    std::unordered_map<std::string, VerbSequenceHandle> ByText;
    std::uint64_t Compiles = 0;
};
