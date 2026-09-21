#pragma once

#include <assets/data/DataAssetHandle.h>
#include <authored/VerbBinding.h>
#include <authored/VerbBindingCompiler.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

class DataAssetCache;

//=============================================================================
// VerbBindingSet
//
// One binding library, instantiated into one World and addressed by key.
//
// The instantiation step is the same for every consumer -- the shell, a screen
// controller, a relay -- so it lives here once instead of being written three
// times with three slightly different answers to "what happens to the binding
// that did not compile".
//
// The answer is: it is left out, and it is reported. A file with one bad
// binding still gives the author the other nine, and the one that is missing
// refuses invocation with a located diagnostic rather than silently doing
// something else.
//
// This is derived state. The authored records stay in the library, which is the
// shared asset; nothing here is edited, and rebuilding it from the library is
// always the way to change it.
//
// A consumer never keeps a pointer into this. Every rebuild moves the
// revision, and a consumer that compiled anything against the set -- an action
// mapping, a relay's resolved record -- compares revisions and looks its
// binding up again by key. A record the new file no longer holds then fails
// the lookup, which is the answer a reload that removed it should get.
//
// The set owns its own reconstruction. It remembers every contribution -- an
// asset it was built from, or a library handed to it in memory -- and Refresh
// rebuilds all of them, in order, when any asset reloaded, when the catalog is
// a different one or has declared or retired anything since, or when the tag
// vocabulary grew. That is what lets a record whose verb was unknown resolve
// once the verb is declared, without every consumer keeping a retry list.
//=============================================================================
class VerbBindingSet
{
public:
    // Replaces everything this set holds with one library's records, kept by
    // copy so a later rebuild can replay it. A reload that removes a binding
    // removes it: the old behaviour must not keep running because the new file
    // stopped mentioning it.
    void Instantiate(const VerbBindingLibrary& library,
                     const VerbBindingEnvironment& environment,
                     std::vector<std::string>& errors);

    // Adds another library's records beside what this set already holds, kept
    // by copy like the first. A key already present is refused and reported
    // rather than replaced: two files claiming one key is an authoring
    // conflict, not a precedence rule.
    void Append(const VerbBindingLibrary& library,
                const VerbBindingEnvironment& environment,
                std::vector<std::string>& errors);

    // The same two operations from a resident asset. The set remembers which
    // assets it was built from and at what reload version, so Refresh can
    // rebuild it when any of them changes.
    void InstantiateFrom(const DataAssetCache& cache,
                         DataAssetHandle asset,
                         const VerbBindingEnvironment& environment,
                         std::vector<std::string>& errors);
    void AppendFrom(const DataAssetCache& cache,
                    DataAssetHandle asset,
                    const VerbBindingEnvironment& environment,
                    std::vector<std::string>& errors);

    // Rebuilds every contribution, in order, if anything it was compiled from
    // or against has changed: an asset's reload version, the catalog's
    // identity or generation, or the tag vocabulary's size. True when it did,
    // in which case the revision moved. Cheap when nothing changed: a handful
    // of integer comparisons, no schema work. A set with no asset sources may
    // pass a null cache.
    [[nodiscard]] bool Refresh(const DataAssetCache* cache,
                               const VerbBindingEnvironment& environment,
                               std::vector<std::string>& errors);

    // Whether any contribution's record failed to compile at the last build.
    // An inspector shows it; Refresh needs no such hint, since what could make
    // a failed record succeed is exactly what it already watches.
    [[nodiscard]] bool HasUnresolved() const { return Unresolved; }

    void Clear();

    // Moves on every rebuild. What a consumer compares before trusting a
    // lookup it made earlier.
    [[nodiscard]] std::uint64_t Revision() const { return Revision_; }

    [[nodiscard]] const CompiledVerbBinding* Find(VerbBindingKey key) const;
    [[nodiscard]] const CompiledVerbBinding* Find(std::string_view key) const;

    [[nodiscard]] std::span<const CompiledVerbBinding> All() const { return Bindings; }
    [[nodiscard]] std::size_t Size() const { return Bindings.size(); }

private:
    // One contribution: an asset, remembered by handle and reload version, or
    // a library handed over in memory, remembered by copy. Either replays.
    struct Source
    {
        DataAssetHandle Asset;
        std::uint64_t ReloadVersion = 0;
        VerbBindingLibrary Library;
    };

    void Compile(const VerbBindingLibrary& library,
                 const VerbBindingEnvironment& environment,
                 std::vector<std::string>& errors);
    void Rebuild(const DataAssetCache* cache,
                 const VerbBindingEnvironment& environment,
                 std::vector<std::string>& errors);
    void Snapshot(const VerbBindingEnvironment& environment);

    // Authored order, and short: a set is one file's bindings, resolved when a
    // consumer is composed or the asset reloads, never per frame.
    std::vector<CompiledVerbBinding> Bindings;
    std::vector<Source> Sources;
    std::uint64_t Revision_ = 0;
    // What the last build compiled against.
    VerbCatalogId Catalog;
    std::uint64_t CatalogGeneration = 0;
    std::size_t TagCount = 0;
    bool Unresolved = false;
};
